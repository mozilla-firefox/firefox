#include "backend.hpp"
#include "common.hpp"
#include "ggml_graph.hpp"   // pk::global_backend()
#include "model_loader.hpp"

#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pk {

// Gallocr buffer size (bytes) after the most recent single-backend (CPU)
// compute. Lets tests assert attention memory scales O(T*window), not O(T^2).
static size_t g_last_graph_alloc_bytes = 0;
size_t last_graph_alloc_bytes() { return g_last_graph_alloc_bytes; }

namespace {
// Number of graph nodes the metadata context must hold. The biggest single
// graph today is the fused encoder. Banded local attention adds O(window) ops
// per layer (~6*(2W+1) nodes), so the encoder caps its window (see
// local_attn_window) to stay within this budget; bumping it globally regresses
// small models (~+22% on tdt_ctc-110m) because the per-compute context + graph
// hash-set scale with kGraphSize. A larger window needs the efficient
// chunk-matmul construction (O(1) nodes) instead.
constexpr size_t kGraphSize = 16384;

struct PendingInput {
    ggml_tensor* tensor;
    const void*  host;
    size_t       nbytes;
};
// Extra graph tensors to read back after compute (besides the final output).
// Used by the fused encoder's forward_capture to pull per-layer outputs out of
// the SAME single graph (vs computing each layer in its own graph). `dst` is the
// caller's vector, alive across the compute call.
struct PendingCapture {
    ggml_tensor*        tensor;
    std::vector<float>* dst;
};
} // namespace

struct Backend::Impl {
    ggml_backend_t       backend     = nullptr;  // primary device (GPU or CPU)
    ggml_backend_t       cpu_backend = nullptr;  // fallback backend (GPU path only)
    ggml_gallocr_t       galloc      = nullptr;  // CPU / single-backend path (unchanged)
    ggml_backend_sched_t sched       = nullptr;  // GPU path: schedules over {backend, cpu_backend}
    bool                 use_sched   = false;    // true only when `backend` is a GPU device
    // Inputs registered by the build lambda for the IN-FLIGHT compute. Copied
    // into the gallocr-allocated tensors after ggml_gallocr_alloc_graph, then
    // cleared. Never overlaps across calls (compute is not re-entrant).
    std::vector<PendingInput> pending;
    // Extra tensors to read back after compute (registered via capture_output).
    std::vector<PendingCapture> captures;
};

// Thread-local pointer to the Backend whose compute() build lambda is currently
// executing, so the free helper add_graph_input() can route registrations
// without threading the Backend through every component's build lambda. compute
// is not re-entrant on a single thread (a build lambda never calls compute), so
// a single pointer is sufficient.
static thread_local Backend* t_active = nullptr;

Backend::Backend(int n_threads) : impl_(new Impl()) {
    // Optional override via PARAKEET_DEVICE:
    //   - "cpu"            forces the CPU backend (CPU baseline on a GPU box).
    //   - a device name    selects that specific registry device by name, e.g.
    //                      "CUDA0", "Vulkan1", "Metal" (case-insensitive).
    //   - unset            auto-pick the first GPU / integrated-GPU device.
    const char* force = std::getenv("PARAKEET_DEVICE");
    const std::string want = force ? force : "";
    const bool force_cpu = want == "cpu" || want == "CPU";

    // Case-insensitive equality, used to match PARAKEET_DEVICE against the
    // registry's device names (which are upper-case like "CUDA0"/"Vulkan0").
    auto iequals = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                return false;
        return true;
    };

    if (!force_cpu) {
        // Walk the registry. Whatever backend was compiled in
        // (CUDA/Metal/Vulkan/HIP/SYCL) registers itself here, so this single path
        // covers them all with no backend-specific includes. Integrated GPUs
        // (e.g. Ryzen APUs) report GGML_BACKEND_DEVICE_TYPE_IGPU and are eligible
        // too. When PARAKEET_DEVICE names a device, match by name; otherwise pick
        // the first GPU/IGPU device.
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            ggml_backend_dev_t dev = ggml_backend_dev_get(i);
            const auto type = ggml_backend_dev_type(dev);
            const char* name = ggml_backend_dev_name(dev);

            bool selected;
            if (!want.empty()) {
                selected = name && iequals(want, name);  // explicit name match
            } else {
                selected = type == GGML_BACKEND_DEVICE_TYPE_GPU ||
                           type == GGML_BACKEND_DEVICE_TYPE_IGPU;
            }
            if (!selected) continue;

            impl_->backend = ggml_backend_dev_init(dev, nullptr);
            if (impl_->backend) {
                device_name_ = name ? name : "";
                // Route compute through ggml_backend_sched for any non-CPU device
                // so unsupported ops can fall back to CPU.
                impl_->use_sched = type != GGML_BACKEND_DEVICE_TYPE_CPU;
                PK_LOG("pk::Backend using device: %s", device_name_.c_str());
                break;
            }
        }
        if (!want.empty() && !impl_->backend)
            PK_LOG("pk::Backend: PARAKEET_DEVICE=%s not found; falling back to CPU",
                   want.c_str());
    }
    if (!impl_->backend) {              // CPU fallback (or CPU-only build)
        impl_->backend = ggml_backend_cpu_init();
        device_name_ = "cpu";
    }
    if (!impl_->backend) {
        PK_LOG("backend init returned null");
        return;
    }
    // GPU path: create a CPU fallback backend so unsupported ops (e.g. CONV_2D_DW,
    // which ggml's Metal backend lacks) are offloaded to CPU by the scheduler
    // instead of aborting. The CPU/single-backend path keeps using the persistent
    // gallocr below and is untouched.
    if (impl_->use_sched) {
        impl_->cpu_backend = ggml_backend_cpu_init();
        if (!impl_->cpu_backend) {
            PK_LOG("pk::Backend: CPU fallback init failed; disabling sched");
            impl_->use_sched = false;
        }
    }
    set_n_threads(n_threads);
}

Backend::~Backend() {
    if (impl_) {
        // Free allocators/scheduler BEFORE the backends they reference.
        if (impl_->sched)       ggml_backend_sched_free(impl_->sched);
        if (impl_->galloc)      ggml_gallocr_free(impl_->galloc);
        if (impl_->cpu_backend) ggml_backend_free(impl_->cpu_backend);
        if (impl_->backend)     ggml_backend_free(impl_->backend);
        delete impl_;
        impl_ = nullptr;
    }
}

void Backend::set_n_threads(int n_threads) {
    n_threads_ = n_threads > 0 ? n_threads : 1;
    if (impl_ && impl_->backend && ggml_backend_is_cpu(impl_->backend)) {
        ggml_backend_cpu_set_n_threads(impl_->backend, n_threads_);
    }
    if (impl_ && impl_->cpu_backend) {
        ggml_backend_cpu_set_n_threads(impl_->cpu_backend, n_threads_);
    }
}

ggml_backend_t Backend::handle() const {
    return impl_ ? impl_->backend : nullptr;
}

void Backend::register_input(ggml_tensor* t, const void* host, size_t nbytes) {
    impl_->pending.push_back({t, host, nbytes});
}

void Backend::register_capture(ggml_tensor* t, std::vector<float>* dst) {
    impl_->captures.push_back({t, dst});
}

bool Backend::compute(const std::function<ggml_tensor*(ggml_context*)>& build,
                      std::vector<float>& out) {
    if (!impl_ || !impl_->backend) {
        PK_LOG("Backend::compute called on an uninitialised backend");
        return false;
    }

    // Metadata-only context: holds graph + tensor structs, no tensor data
    // (no_alloc=true). Tensor data lives in the gallocr's persistent buffer.
    struct ggml_init_params params = {
        /* .mem_size   = */ ggml_tensor_overhead() * kGraphSize +
                            ggml_graph_overhead_custom(kGraphSize, false),
        /* .mem_buffer = */ nullptr,
        /* .no_alloc   = */ true,
    };
    struct ggml_context* ctx = ggml_init(params);
    if (!ctx) {
        PK_LOG("Backend::compute: ggml_init failed");
        return false;
    }

    // Drive add_graph_input()/capture registrations to this Backend for the
    // build call.
    impl_->pending.clear();
    impl_->captures.clear();
    Backend* prev_active = t_active;
    t_active = this;
    struct ggml_tensor* output = build(ctx);
    t_active = prev_active;

    if (!output) {
        PK_LOG("Backend::compute: build() returned null output tensor");
        impl_->pending.clear();
        impl_->captures.clear();
        ggml_free(ctx);
        return false;
    }
    // Mark the output (and any captured tensors) so the gallocr does not recycle
    // their storage before we read them back, then expand the forward graph.
    ggml_set_output(output);
    for (const PendingCapture& pc : impl_->captures) ggml_set_output(pc.tensor);

    struct ggml_cgraph* gf = ggml_new_graph_custom(ctx, kGraphSize, false);
    // Expand captures FIRST so they are present in the graph even if the final
    // output's subgraph does not reach them (it does here, but be robust).
    for (const PendingCapture& pc : impl_->captures)
        ggml_build_forward_expand(gf, pc.tensor);
    ggml_build_forward_expand(gf, output);

    // GPU devices default to the fast persistent-gallocr path (identical to a
    // single-backend run). Only route THIS graph through the scheduler (which
    // offloads unsupported ops to CPU) when the GPU backend actually lacks a
    // kernel for one of its ops. CUDA covers every op parakeet uses, so it stays
    // on gallocr with zero scheduler overhead; Metal likewise once its kernels
    // are present; a genuinely missing op still degrades gracefully to CPU. The
    // per-graph check is a cheap O(nodes) scan, far less than a sched re-plan.
    bool need_sched = false;
    if (impl_->use_sched) {
        const int n_nodes = ggml_graph_n_nodes(gf);
        for (int i = 0; i < n_nodes; ++i) {
            if (!ggml_backend_supports_op(impl_->backend, ggml_graph_node(gf, i))) {
                need_sched = true;
                break;
            }
        }
    }

    bool alloc_ok = false;
    if (need_sched) {
        // GPU path: schedule across {GPU, CPU}. Unsupported ops fall back to CPU.
        if (!impl_->sched) {
            ggml_backend_t backs[2] = { impl_->backend, impl_->cpu_backend };
            impl_->sched = ggml_backend_sched_new(
                backs, /*bufts=*/nullptr, /*n_backends=*/2,
                /*graph_size=*/kGraphSize, /*parallel=*/false, /*op_offload=*/true);
            if (!impl_->sched) {
                PK_LOG("Backend::compute: ggml_backend_sched_new failed");
                impl_->pending.clear();
                impl_->captures.clear();
                ggml_free(ctx);
                return false;
            }
        }
        ggml_backend_sched_reset(impl_->sched);
        alloc_ok = ggml_backend_sched_alloc_graph(impl_->sched, gf);
        if (!alloc_ok) PK_LOG("Backend::compute: ggml_backend_sched_alloc_graph failed");
    } else {
        // Fast path: CPU, or a GPU whose backend supports every op in this graph.
        // Persistent gallocr over the active backend's buffer type, lazily created
        // and reused on every subsequent call (it only reallocates the underlying
        // buffer when the graph grows beyond the current high-water mark). This is
        // the original single-backend path; weights stay zero-copy on the device.
        if (!impl_->galloc) {
            impl_->galloc = ggml_gallocr_new(
                ggml_backend_get_default_buffer_type(impl_->backend));
            if (!impl_->galloc) {
                PK_LOG("Backend::compute: ggml_gallocr_new failed");
                impl_->pending.clear();
                impl_->captures.clear();
                ggml_free(ctx);
                return false;
            }
        }
        alloc_ok = ggml_gallocr_alloc_graph(impl_->galloc, gf);
        if (!alloc_ok) PK_LOG("Backend::compute: ggml_gallocr_alloc_graph failed");
        else g_last_graph_alloc_bytes = ggml_gallocr_get_buffer_size(impl_->galloc, 0);
    }
    if (!alloc_ok) {
        impl_->pending.clear();
        impl_->captures.clear();
        ggml_free(ctx);
        return false;
    }

    // Inputs are allocated now (->buffer/->data set): push host data in.
    for (const PendingInput& pi : impl_->pending) {
        ggml_backend_tensor_set(pi.tensor, pi.host, 0, pi.nbytes);
    }
    impl_->pending.clear();

    enum ggml_status status = need_sched
        ? ggml_backend_sched_graph_compute(impl_->sched, gf)
        : ggml_backend_graph_compute(impl_->backend, gf);
    if (status != GGML_STATUS_SUCCESS) {
        PK_LOG("Backend::compute: ggml_backend_graph_compute failed (status=%d)",
               (int)status);
        impl_->captures.clear();
        ggml_free(ctx);
        return false;
    }

    // Read back any captured intermediates (per-layer outputs), then the final
    // output.
    for (const PendingCapture& pc : impl_->captures) {
        size_t cn = (size_t)ggml_nelements(pc.tensor);
        pc.dst->resize(cn);
        ggml_backend_tensor_get(pc.tensor, pc.dst->data(), 0, cn * sizeof(float));
    }
    impl_->captures.clear();

    size_t n = (size_t)ggml_nelements(output);
    out.resize(n);
    ggml_backend_tensor_get(output, out.data(), 0, n * sizeof(float));

    ggml_free(ctx);
    return true;
}

void add_graph_input(ggml_tensor* t, const void* host, size_t nbytes) {
    GGML_ASSERT(t_active != nullptr &&
                "add_graph_input called outside a Backend::compute build lambda");
    ggml_set_input(t);
    t_active->register_input(t, host, nbytes);
}

ggml_tensor* graph_input_tensor(ggml_context* ctx, int type, int n_dims,
                                const int64_t* ne, const void* host,
                                size_t nbytes) {
    ggml_tensor* t = ggml_new_tensor(ctx, (ggml_type)type, n_dims, ne);
    add_graph_input(t, host, nbytes);
    return t;
}

void capture_graph_output(ggml_tensor* t, std::vector<float>* dst) {
    GGML_ASSERT(t_active != nullptr &&
                "capture_graph_output called outside a Backend::compute build lambda");
    t_active->register_capture(t, dst);
}

bool ensure_weights_realized(const ModelLoader& ml) {
    if (ml.weights_realized()) return true;
    // realize_weights mutates tensor->buffer; the ModelLoader is held by `const`
    // ref throughout the inference path (the components are read-only views), but
    // realizing the backend buffer is a one-time, semantically-const setup of the
    // weight storage. Cast away const for that single call.
    ModelLoader& mut = const_cast<ModelLoader&>(ml);
    return mut.realize_weights(global_backend().handle());
}

ggml_tensor* clone_weight(ggml_context* /*ctx*/, const ModelLoader& ml,
                          const char* name) {
    // Zero-copy: ensure the loader's weights have a backend buffer once, then
    // return the loader tensor DIRECTLY. It has ->data + ->buffer set, so the
    // gallocr treats it as already-allocated (never copies it) and the CPU
    // backend reads its bytes in place. Downstream reshapes/views of the
    // returned tensor resolve their data pointer at build time (the src has
    // data), so no per-call weight copy ever happens.
    ensure_weights_realized(ml);
    ggml_tensor* src = ml.tensor(name);
    assert(src && "missing tensor");
    return src;
}

ggml_tensor* clone_weight_opt(ggml_context* ctx, const ModelLoader& ml,
                              const char* name) {
    if (!ml.tensor(name)) return nullptr;
    return clone_weight(ctx, ml, name);
}

void weight_to_host_f32(const ModelLoader& ml, const char* name, std::vector<float>& out) {
    ensure_weights_realized(ml);
    ggml_tensor* t = ml.tensor(name);
    GGML_ASSERT(t && "weight_to_host_f32: missing tensor");
    GGML_ASSERT(t->type == GGML_TYPE_F32 && "weight_to_host_f32: tensor not f32");
    out.resize((size_t)ggml_nelements(t));
    ggml_backend_tensor_get(t, out.data(), 0, ggml_nbytes(t));
}

} // namespace pk
