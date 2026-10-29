
#include "bitdowner.h"

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kBlockFrames = 4096;

void print_help(const char* program) {
    std::cout
        << "BitDowner - offline audio bit reduction effect\n\n"
        << "Usage:\n"
        << "  " << program << " INPUT OUTPUT [OPTIONS]\n\n"
        << "Options:\n"
        << "  --input-gain DB    Input gain, -40 to 40 (default: 0)\n"
        << "  --bit-down VALUE   BitDown control, 1 to 16 (default: 16)\n"
        << "  --downsampling N   Sample-hold interval, 1 to 200 (default: 1)\n"
        << "  --volume DB        Output gain, -90 to 12 (default: 0)\n"
        << "  -h, --help         Show this help\n\n"
        << "Example:\n"
        << "  " << program
        << " input.wav output.wav --input-gain 6"
        << " --bit-down 8 --downsampling 4 --volume -3\n";
}

double parse_number(const std::string& option, const std::string& value) {
    std::size_t used = 0;
    double result;

    try {
        result = std::stod(value, &used);
    } catch (...) {
        throw std::invalid_argument(
            "invalid number for " + option + ": " + value);
    }

    if (used != value.size() || !std::isfinite(result)) {
        throw std::invalid_argument(
            "invalid number for " + option + ": " + value);
    }

    return result;
}

struct Arguments {
    std::string input_path;
    std::string output_path;
    BitDowner::Parameters params;
};

Arguments parse_arguments(int argc, char** argv) {
    Arguments args;
    bool have_input = false;
    bool have_output = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        }

        if (arg == "--input-gain" ||
            arg == "--bit-down" ||
            arg == "--downsampling" ||
            arg == "--volume") {

            if (i + 1 >= argc) {
                throw std::invalid_argument(
                    "missing value after " + arg);
            }

            const double value = parse_number(arg, argv[++i]);

            if (arg == "--input-gain") {
                args.params.input_gain_db = value;
            } else if (arg == "--bit-down") {
                args.params.bit_down = value;
            } else if (arg == "--downsampling") {
                args.params.downsampling = value;
            } else if (arg == "--volume") {
                args.params.volume_db = value;
            }

            continue;
        }

        if (arg.rfind("--", 0) == 0) {
            throw std::invalid_argument("unknown option: " + arg);
        }

        if (!have_input) {
            args.input_path = arg;
            have_input = true;
        } else if (!have_output) {
            args.output_path = arg;
            have_output = true;
        } else {
            throw std::invalid_argument("unexpected argument: " + arg);
        }
    }

    if (!have_input || !have_output) {
        print_help(argv[0]);
        throw std::invalid_argument(
            "both input and output paths are required");
    }

    if (args.input_path == args.output_path) {
        throw std::invalid_argument(
            "input and output paths must be different");
    }

    return args;
}

class SoundFile {
public:
    SoundFile(const std::string& path, int mode, SF_INFO& info)
        : file_(sf_open(path.c_str(), mode, &info)) {
        if (!file_) {
            throw std::runtime_error(
                path + ": " + sf_strerror(nullptr));
        }
    }

    ~SoundFile() {
        if (file_) {
            sf_close(file_);
        }
    }

    SoundFile(const SoundFile&) = delete;
    SoundFile& operator=(const SoundFile&) = delete;

    SNDFILE* get() const {
        return file_;
    }

private:
    SNDFILE* file_ = nullptr;
};

} // namespace

int main(int argc, char** argv) {
    try {
        const Arguments args = parse_arguments(argc, argv);

        // Validate DSP parameters before opening the output.
        BitDowner validator(args.params);

        SF_INFO input_info{};
        SoundFile input_file(args.input_path, SFM_READ, input_info);

        if (input_info.frames < 0 ||
            input_info.samplerate <= 0 ||
            input_info.channels <= 0) {
            throw std::runtime_error("invalid input audio format");
        }

        if (input_info.channels > 256) {
            throw std::runtime_error(
                "unsupported channel count (maximum 256)");
        }

        // Output WAV with 32-bit floating-point samples.
        SF_INFO output_info{};
        output_info.samplerate = input_info.samplerate;
        output_info.channels = input_info.channels;
        output_info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        if (!sf_format_check(&output_info)) {
            throw std::runtime_error(
                "cannot create output WAV with this format");
        }

        SoundFile output_file(
            args.output_path, SFM_WRITE, output_info);

        std::vector<BitDowner> processors;
        processors.reserve(input_info.channels);

        for (int ch = 0; ch < input_info.channels; ++ch) {
            processors.emplace_back(args.params);
        }

        const std::size_t channels =
            static_cast<std::size_t>(input_info.channels);

        std::vector<float> interleaved_input(
            static_cast<std::size_t>(kBlockFrames) * channels);
        std::vector<float> interleaved_output(
            static_cast<std::size_t>(kBlockFrames) * channels);

        std::array<std::vector<float>, 256> channel_input;
        std::array<std::vector<float>, 256> channel_output;

        for (int ch = 0; ch < input_info.channels; ++ch) {
            channel_input[ch].resize(kBlockFrames);
            channel_output[ch].resize(kBlockFrames);
        }

        sf_count_t total_frames = 0;
        sf_count_t frames_read = 0;

        while ((frames_read = sf_readf_float(
                    input_file.get(),
                    interleaved_input.data(),
                    kBlockFrames)) > 0) {

            // Deinterleave, process each channel, then interleave.
            for (sf_count_t frame = 0; frame < frames_read; ++frame) {
                for (int ch = 0; ch < input_info.channels; ++ch) {
                    const auto index =
                        static_cast<std::size_t>(frame) * channels + ch;

                    channel_input[ch][static_cast<std::size_t>(frame)] =
                        interleaved_input[index];
                }
            }

            for (int ch = 0; ch < input_info.channels; ++ch) {
                processors[ch].process(
                    channel_input[ch].data(),
                    channel_output[ch].data(),
                    static_cast<int>(frames_read));
            }

            for (sf_count_t frame = 0; frame < frames_read; ++frame) {
                for (int ch = 0; ch < input_info.channels; ++ch) {
                    const auto index =
                        static_cast<std::size_t>(frame) * channels + ch;

                    interleaved_output[index] =
                        channel_output[ch][static_cast<std::size_t>(frame)];
                }
            }

            const sf_count_t written = sf_writef_float(
                output_file.get(),
                interleaved_output.data(),
                frames_read);

            if (written != frames_read) {
                throw std::runtime_error(
                    "failed to write all output audio frames");
            }

            total_frames += frames_read;
        }

        if (sf_error(input_file.get()) != SF_ERR_NO_ERROR) {
            throw std::runtime_error(
                std::string("error reading input: ") +
                sf_strerror(input_file.get()));
        }

        sf_write_sync(output_file.get());

        std::cout << "Processed: " << args.input_path << '\n'
                  << "Output:    " << args.output_path << '\n'
                  << "Sample rate: " << input_info.samplerate << " Hz\n"
                  << "Channels:    " << input_info.channels << '\n'
                  << "Frames:      " << total_frames << '\n'
                  << "Duration:    " << std::fixed << std::setprecision(2)
                  << static_cast<double>(total_frames) /
                         input_info.samplerate
                  << " seconds\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "bitdowner: " << e.what() << '\n';
        return 1;
    }
}
