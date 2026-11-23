#include "diarization.hpp"
#include "diarization_streaming.hpp"

#include "audio_io.hpp"
#include "backend.hpp"
#include "ggml_graph.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include "moz-overrides.h"

namespace pk {

std::unique_ptr<DiarizationModel> DiarizationModel::load(const std::string& path) {
    // unique_ptr<DiarizationModel> via private ctor: construct then load.
    std::unique_ptr<DiarizationModel> m(new (std::nothrow) DiarizationModel());
    if (!m) return nullptr;
    if (!m->loader_.load(path)) return nullptr;

    const auto& cfg = m->loader_.config();
    if (cfg.arch != "diarization") {
        // Not a diarization model — caller should use Model for ASR.
        return nullptr;
    }
    if (!cfg.diarization.present) {
        return nullptr;
    }

    // Give the weights a backend buffer ONCE so graphs reference them
    // directly as leaves (zero per-call copy), same as Model::load.
    if (!ensure_weights_realized(m->loader_)) return nullptr;

    // Construct the component objects (lightweight views over the ModelLoader).
    m->mel_     = std::make_unique<MelFrontend>(m->loader_);
    m->encoder_ = std::make_unique<DiarizationEncoder>(m->loader_);
    m->head_    = std::make_unique<DiarizationHead>(m->loader_);

    return m;
}

DiarizationResult DiarizationModel::diarize_path(const std::string& wav_path) {
    Audio audio;
    if (!load_audio_16k_mono(wav_path, audio)) {
        throw std::runtime_error("parakeet: failed to load audio: " + wav_path);
    }
    // load_audio_16k_mono already resamples to 16 kHz mono.
    return run(audio.samples);
}

DiarizationResult DiarizationModel::diarize_pcm(
        const std::vector<float>& samples, int sample_rate) {
    if (sample_rate <= 0) {
        throw std::runtime_error("parakeet: invalid sample_rate");
    }
    if (sample_rate == 16000) {
        return run(samples);
    }
    std::vector<float> pcm16k = resample_linear(samples, sample_rate, 16000);
    return run(pcm16k);
}

void DiarizationModel::speaker_probs(const std::vector<float>& samples,
                                     std::vector<float>& probs,
                                     int& n_spk, int& T) const {
    const ParakeetConfig& cfg = loader_.config();
    n_spk = (int)cfg.diarization.n_speakers;
    T = 0;
    probs.clear();

    // 1. Log-mel front end -> feats [n_mels, T]
    // NeMo SortformerEncLabelModel.process_signal peak-normalizes the waveform
    // in offline (non-streaming) mode: x * 1 / (max(x) + eps), eps = 1e-3.
    // Note max(x), not max(|x|).
    std::vector<float> norm(samples);
    if (!norm.empty()) {
        const float peak = *std::max_element(norm.begin(), norm.end());
        const float scale = 1.0f / (peak + 1e-3f);
        for (float& v : norm) v *= scale;
    }
    std::vector<float> feats;
    int n_mels = 0, T_mel = 0;
    mel_->compute(norm, feats, n_mels, T_mel);

    // NeMo trims the features to the valid length floor(S / hop) before the
    // encoder; the centered STFT yields one extra frame past it.
    const int hop = (int)cfg.hop_length;
    const int T_valid = hop > 0 ? std::min(T_mel, (int)(samples.size() / hop)) : T_mel;
    if (T_valid < T_mel) {
        std::vector<float> trimmed((size_t)n_mels * T_valid);
        for (int m = 0; m < n_mels; ++m)
            std::copy_n(feats.begin() + (size_t)m * T_mel, T_valid,
                        trimmed.begin() + (size_t)m * T_valid);
        feats.swap(trimmed);
        T_mel = T_valid;
    }
    if (T_mel == 0) return;

    // 2. Diarization encoder -> enc_out [T_enc, d_model] (time-major)
    std::vector<float> enc_out;
    int T_enc = 0;
    encoder_->forward(feats, n_mels, T_mel, enc_out, T_enc);

    // 3. Diarization head -> probs [n_spk, T_out] (post-sigmoid)
    int T_out = 0;
    head_->forward(enc_out, T_enc, probs, n_spk, T_out);

    // High-resolution output has one frame per mel frame; drop the frames the
    // FeatureStacking pad added past T_mel (NeMo slices preds to the mel length).
    if (T_out > T_mel) {
        std::vector<float> cut((size_t)n_spk * T_mel);
        for (int s = 0; s < n_spk; ++s)
            std::copy_n(probs.begin() + (size_t)s * T_out, T_mel,
                        cut.begin() + (size_t)s * T_mel);
        probs.swap(cut);
        T_out = T_mel;
    }
    T = T_out;
}

DiarizationResult DiarizationModel::run(const std::vector<float>& samples) {
    // NeMo's diarize() follows the checkpoint's streaming_mode (true for
    // Nemotron-3-Diarization). The streaming path is also the one that holds
    // up on long audio: offline attends over the whole clip, far beyond the
    // training sessions (and quadratic in length).
    return loader_.config().diarization.streaming_mode ? run_streaming(samples)
                                                       : run_offline(samples);
}

DiarizationResult DiarizationModel::run_offline(const std::vector<float>& samples) const {
    std::vector<float> probs;
    int n_spk = 0, T = 0;
    speaker_probs(samples, probs, n_spk, T);
    DiarizationResult result;
    result.segments = segments_from_probs(probs, n_spk, T);
    result.n_speakers = (int)loader_.config().diarization.n_speakers;
    return result;
}

DiarizationResult DiarizationModel::run_streaming(const std::vector<float>& samples) const {
    const ParakeetConfig& cfg = loader_.config();
    DiarizationResult result;
    result.n_speakers = (int)cfg.diarization.n_speakers;

    // Whole-clip log-mel, not peak-normalized in streaming mode, trimmed to
    // floor(S / hop) frames like NeMo.
    std::vector<float> feats;
    int n_mels = 0, T = 0;
    mel_->compute(samples, feats, n_mels, T);
    if (cfg.hop_length > 0) T = std::min(T, (int)(samples.size() / cfg.hop_length));
    if (T <= 0) return result;
    const int T_full = (int)(feats.size() / n_mels);

    // Whole clip in one feed: the diarizer chunks it with the checkpoint's
    // streaming configuration, exactly as NeMo's diarize() does.
    if ((int)(feats.size() / n_mels) != T) {
        std::vector<float> trimmed((size_t)n_mels * T);
        for (int m = 0; m < n_mels; ++m)
            std::copy_n(feats.begin() + (size_t)m * T_full, T, trimmed.begin() + (size_t)m * T);
        feats.swap(trimmed);
    }
    StreamingDiarization sd(loader_);
    for (const auto& g : sd.feed_mel(feats, n_mels, T, /*is_last=*/true))
        result.segments.push_back({g.speaker, g.start, g.end});
    std::sort(result.segments.begin(), result.segments.end(),
              [](const SpeakerSegment& a, const SpeakerSegment& b) {
                  return a.start != b.start ? a.start < b.start : a.speaker < b.speaker;
              });
    return result;
}

std::vector<SpeakerSegment> DiarizationModel::segments_from_probs(
        const std::vector<float>& probs, int n_spk, int T) const {
    const auto& d = loader_.config().diarization;
    return postprocess(probs, n_spk, T, d.frame_resolution_sec, d.onset_threshold,
                       d.offset_threshold);
}

std::vector<SpeakerSegment> DiarizationModel::postprocess(
        const std::vector<float>& probs, int n_spk, int T_out,
        float frame_sec, float onset, float offset) const {

    // NeMo predlist_to_timestamps (from nemo.collections.asr.parts.utils.vad_utils):
    //
    // 1. Hysteresis binarization per speaker:
    //    - OFF → ON when prob >= onset
    //    - ON → OFF when prob < offset
    //    When onset == offset (0.5 for Nemotron-3), this is a simple threshold.
    //
    // 2. Extract contiguous ON segments per speaker.
    //
    // 3. min_duration_on / min_duration_off filtering (defaults 0.0 → no-op).
    //
    // 4. merge_overlap_segment: merges same-speaker segments that overlap
    //    (from padded/chunked inference). Offline single-pass produces no
    //    overlaps, so this is a no-op here.
    //
    // 5. Round timestamps to 2 decimal places.

    // probs is row-major [n_spk, T_out]: probs[s * T_out + t]
    std::vector<SpeakerSegment> segments;

    for (int s = 0; s < n_spk; ++s) {
        const float* p = probs.data() + (size_t)s * T_out;

        bool active = false;
        int start_frame = 0;

        for (int t = 0; t < T_out; ++t) {
            const bool on = (p[t] >= onset);
            if (on && !active) {
                // Hysteresis: OFF → ON at onset threshold
                start_frame = t;
                active = true;
            } else if (!on && active) {
                // Hysteresis: ON → OFF when prob drops below offset.
                // With onset == offset, p[t] < offset ⟺ p[t] < onset ⟺ !on.
                float start_sec = start_frame * frame_sec;
                float end_sec   = t * frame_sec;
                segments.push_back({s, start_sec, end_sec});
                active = false;
            }
        }
        // Close any segment still open at the end of the audio.
        if (active) {
            float start_sec = start_frame * frame_sec;
            float end_sec   = T_out * frame_sec;
            segments.push_back({s, start_sec, end_sec});
        }
    }

    // Sort by start time, then by speaker (NeMo returns segments in start order).
    std::sort(segments.begin(), segments.end(),
              [](const SpeakerSegment& a, const SpeakerSegment& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.speaker < b.speaker;
              });

    // Round to 2 decimal places (NeMo uses round(ts, 2)).
    for (auto& seg : segments) {
        seg.start = std::round(seg.start * 100.0f) / 100.0f;
        seg.end   = std::round(seg.end   * 100.0f) / 100.0f;
    }

    return segments;
}

} // namespace pk
