
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

class BitDowner {
public:
    struct Parameters {
        double input_gain_db = 0.0;
        double bit_down = 16.0;
        double downsampling = 1.0;
        double volume_db = 0.0;
    };

    
    BitDowner() {
        set_parameters(Parameters{});
        reset();
    }

    explicit BitDowner(const Parameters& params) {
        set_parameters(params);
        reset();
    }


    void set_parameters(const Parameters& params) {
        if (!std::isfinite(params.input_gain_db) ||
            params.input_gain_db < -40.0 ||
            params.input_gain_db > 40.0) {
            throw std::invalid_argument(
                "input gain must be between -40 and +40 dB");
        }

        if (!std::isfinite(params.bit_down) ||
            params.bit_down < 1.0 || params.bit_down > 16.0) {
            throw std::invalid_argument(
                "bit-down must be between 1 and 16");
        }

        if (!std::isfinite(params.downsampling) ||
            params.downsampling < 1.0 ||
            params.downsampling > 200.0) {
            throw std::invalid_argument(
                "downsampling must be between 1 and 200");
        }

        if (!std::isfinite(params.volume_db) ||
            params.volume_db < -90.0 ||
            params.volume_db > 12.0) {
            throw std::invalid_argument(
                "volume must be between -90 and +12 dB");
        }

        params_ = params;

        // Match the original generated DSP equations.
        input_target_ =
            0.001 * std::pow(10.0, 0.05 * params_.input_gain_db);

        quantization_scale_ =
            std::pow(2.0, params_.bit_down - 1.0);

        inverse_scale_ = 1.0 / quantization_scale_;

        downsample_count_ = static_cast<int>(params_.downsampling);

        output_target_ =
            0.001 * std::pow(10.0, 0.05 * params_.volume_db);
    }

    void reset() {
        input_gain_state_ = 0.0;
        output_gain_state_ = 0.0;
        held_sample_ = 0.0;
        sample_counter_ = 0;
    }

    void process(const float* input, float* output, int count) {
        for (int i = 0; i < count; ++i) {
            // Input gain smoothing, as in the original DSP.
            input_gain_state_ =
                input_target_ + 0.999 * input_gain_state_;

            const double quantized =
                inverse_scale_ *
                std::floor(
                    quantization_scale_ *
                    static_cast<double>(input[i]) *
                    input_gain_state_);

            const double clipped =
                std::clamp(quantized, -1.0, 1.0);

            // Original sample-hold counter.
            if (sample_counter_ < downsample_count_ - 1) {
                ++sample_counter_;
            } else {
                sample_counter_ = 0;
            }

            if (sample_counter_ == 0) {
                held_sample_ = clipped;
            }

            const double signal =
                (downsample_count_ > 1)
                    ? held_sample_
                    : clipped;

            // Output gain smoothing, as in the original DSP.
            output_gain_state_ =
                output_target_ + 0.999 * output_gain_state_;

            output[i] = static_cast<float>(
                output_gain_state_ * signal);
        }
    }

private:
    Parameters params_;

    double input_target_ = 0.0;
    double quantization_scale_ = 0.0;
    double inverse_scale_ = 0.0;
    double output_target_ = 0.0;

    int downsample_count_ = 1;
    int sample_counter_ = 0;

    double input_gain_state_ = 0.0;
    double output_gain_state_ = 0.0;
    double held_sample_ = 0.0;
};
