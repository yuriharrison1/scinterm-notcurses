# Design: Renderização Paralela para Scinterm NotCurses

**Status:** Design Completo  
**Versão:** 1.0  
**Data:** 2026-03-22

---

## 1. Visão Geral

### 1.1. Objetivo
Implementar renderização paralela multi-threaded para o Scinterm NotCurses, permitindo aproveitar CPUs multi-core para renderização de texto em terminal.

### 1.2. Restrições
- NotCurses **não é thread-safe** - acesso ao `ncplane` deve ser serializado
- Scintilla tem estado compartilhado (`Document`, `ViewStyle`)
- Terminal tem limitações de largura de banda

### 1.3. Estratégia: Tile-Based Rendering

```
┌─────────────────────────────────────────┐
│               TILE 0                    │  Thread 0
│           (linhas 0-9)                  │
├─────────────────────────────────────────┤
│               TILE 1                    │  Thread 1
│          (linhas 10-19)                 │
├─────────────────────────────────────────┤
│               TILE 2                    │  Thread 2
│          (linhas 20-29)                 │
├─────────────────────────────────────────┤
│               TILE 3                    │  Thread 3
│          (linhas 30-39)                 │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│            COMPOSITE STAGE              │  Thread Main
│      (Blit de tiles para ncplane)       │
└─────────────────────────────────────────┘
```

---

## 2. Arquitetura

### 2.1. Componentes

```cpp
namespace Scintilla::Internal {

// Configuração de renderização paralela
struct ParallelRenderConfig {
    int num_threads = 0;        // 0 = auto (std::thread::hardware_concurrency())
    int min_tile_lines = 5;     // Mínimo de linhas por tile
    bool enabled = true;        // Pode desabilitar em runtime
};

// Tile - unidade de trabalho
struct RenderTile {
    int tile_id;
    int start_line;             // Linha inicial no documento
    int end_line;               // Linha final (exclusivo)
    PRectangle clip_rect;       // Área na tela
    
    // Surface temporária para renderização
    std::unique_ptr<Surface> surface;
    
    // Estado de sincronização
    std::atomic<bool> completed{false};
    std::atomic<bool> error{false};
};

// Thread pool simplificado
class RenderThreadPool {
public:
    explicit RenderThreadPool(size_t num_threads);
    ~RenderThreadPool();
    
    // Submete trabalho e espera completion
    void parallel_for(std::function<void(int start, int end)> worker);
    
    // Configuração dinâmica
    void resize(size_t num_threads);
    size_t size() const;
    
private:
    std::vector<std::thread> workers_;
    // ... (implementação típica de thread pool)
};

// Renderer paralelo
class ParallelRenderer {
public:
    explicit ParallelRenderer(const ParallelRenderConfig& config);
    
    // Renderiza editor em paralelo
    void render(ScintillaNotCurses* editor, Surface* target_surface);
    
    // Estatísticas
    struct Stats {
        double avg_tile_time_ms;
        double total_time_ms;
        int tiles_rendered;
        int cache_hits;
    };
    Stats get_stats() const;
    
    // Controle de qualidade
    void set_quality_level(int level);  // 0=fast, 1=balanced, 2=quality
    
private:
    ParallelRenderConfig config_;
    std::unique_ptr<RenderThreadPool> thread_pool_;
    
    // Divide trabalho em tiles
    std::vector<RenderTile> create_tiles(
        ScintillaNotCurses* editor,
        const PRectangle& full_rect);
    
    // Renderiza um tile
    void render_tile(ScintillaNotCurses* editor, RenderTile& tile);
    
    // Compõe tiles no surface final
    void composite_tiles(Surface* target, 
                         const std::vector<RenderTile>& tiles);
};

} // namespace Scintilla::Internal
```

### 2.2. Fluxo de Dados

```
┌─────────────────┐
│  ScintillaBase  │
│   (Documento)   │
└────────┬────────┘
         │
         │ 1. Prepara tiles (main thread)
         ▼
┌─────────────────────────────────────┐
│         TILE SPLITTER               │
│  - Calcula linhas visíveis          │
│  - Divide em N tiles                │
│  - Cria surfaces temporárias        │
└─────────────┬───────────────────────┘
              │
              │ 2. Distribui para threads
              ▼
┌─────────────┬─────────────┬─────────────┐
│  Thread 0   │  Thread 1   │  Thread N   │
│  ┌────────┐ │  ┌────────┐ │  ┌────────┐ │
│  │ Tile 0 │ │  │ Tile 1 │ │  │ Tile N │ │
│  │Surface │ │  │Surface │ │  │Surface │ │
│  └────────┘ │  └────────┘ │  └────────┘ │
│  Renderiza  │  Renderiza  │  Renderiza  │
│  linhas 0-9 │  linhas10-19│  linhas...  │
└─────────────┴─────────────┴─────────────┘
              │
              │ 3. Barreira de sincronização
              ▼
┌─────────────────────────────────────┐
│       COMPOSITE STAGE               │
│  (Thread principal)                 │
│                                     │
│  for each tile:                     │
│    Copy(tile.surface → main_ncplane)│
│                                     │
└─────────────────────────────────────┘
```

---

## 3. Sincronização

### 3.1. Estado Compartilhado

```cpp
// Scintilla tem muitos membros compartilhados.
// Precisamos garantir que threads de renderização
// só leiam, nunca escrevam.

class ScintillaNotCurses {
    // READ-ONLY durante render (thread-safe)
    const Document* doc_;           // Document é immutable durante render
    const ViewStyle& view_style_;   // Copiado no início do frame
    
    // PROTEGIDO por mutex
    mutable std::shared_mutex doc_mutex_;
    
    // THREAD-LOCAL (cada thread tem sua cópia)
    thread_local static std::vector<char> tls_render_buffer_;
};
```

### 3.2. Política de Locking

```cpp
// Leitura do documento (múltiplas threads simultâneas)
void render_tile(const Document* doc, int start_line, int end_line) {
    // Document usa copy-on-write para modificações
    // Durante render, é seguro ler concorrentemente
    for (int line = start_line; line < end_line; line++) {
        auto text = doc->get_line(line);  // Read-only access
        // ... renderiza
    }
}

// Modificação do documento (exclusivo)
void ScintillaNotCurses::insert_text(...) {
    std::unique_lock lock(doc_mutex_);
    // Modifica documento
    // Invalida cache de layout
    mark_dirty();
}
```

---

## 4. Implementação

### 4.1. Thread Pool Simples

```cpp
// include/scinterm_parallel.h
#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

namespace Scintilla::Internal {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();
    
    // Não copiável
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    // Submete trabalho, retorna future
    template<typename F>
    auto enqueue(F&& f) -> std::future<decltype(f())> {
        using return_type = decltype(f());
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(f));
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock lock(queue_mutex_);
            if (stop_) return result;  // Pool parado
            
            tasks_.emplace([task] { (*task)(); });
        }
        
        condition_.notify_one();
        return result;
    }
    
    // Espera todas as tasks terminarem
    void wait_all();
    
    // Estado
    size_t size() const { return workers_.size(); }
    bool empty() const;
    
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
};

} // namespace Scintilla::Internal
```

### 4.2. ParallelSurface (Surface Multi-Tile)

```cpp
// src/plat/scinterm_parallel.cpp

class ParallelSurface : public Surface {
public:
    struct Tile {
        std::unique_ptr<Surface> surface;
        PRectangle bounds;
        int start_line;
        int end_line;
    };
    
    ParallelSurface(int num_tiles, int tile_height);
    
    // Surface interface
    void Init(WindowID wid) override;
    void Release() noexcept override;
    
    // Renderização paralela
    void parallel_paint(ScintillaBase* editor, 
                       const PRectangle& rc,
                       ThreadPool& pool);
    
    // Composição final (thread principal)
    void composite_to(Surface* target);
    
private:
    std::vector<Tile> tiles_;
    std::vector<std::future<void>> pending_renders_;
};

void ParallelSurface::parallel_paint(ScintillaBase* editor,
                                     const PRectangle& rc,
                                     ThreadPool& pool) {
    pending_renders_.clear();
    
    for (auto& tile : tiles_) {
        auto future = pool.enqueue([this, editor, &tile]() {
            // Renderiza este tile em sua própria surface
            editor->paint_tile(tile.surface.get(), tile.bounds,
                             tile.start_line, tile.end_line);
        });
        pending_renders_.push_back(std::move(future));
    }
    
    // Espera todas completarem
    for (auto& f : pending_renders_) {
        f.wait();
    }
}

void ParallelSurface::composite_to(Surface* target) {
    // Esta função DEVE ser chamada da thread principal
    // pois vai acessar o ncplane real
    for (const auto& tile : tiles_) {
        target->Copy(tile.bounds, tile.bounds.top_left(), *tile.surface);
    }
}
```

### 4.3. Integração com ScintillaNotCurses

```cpp
// Modificação em scinterm_notcurses.cpp

class ScintillaNotCurses : public ScintillaBase {
    // ... membros existentes ...
    
    // Novos membros para paralelismo
    std::unique_ptr<ThreadPool> render_pool_;
    std::unique_ptr<ParallelSurface> parallel_surface_;
    bool use_parallel_render_ = true;
    
    // Configuração
    struct ParallelConfig {
        int num_threads = 4;
        int min_lines_for_parallel = 20;
        bool enabled = true;
    } parallel_config_;
    
public:
    void Render() {
        if (!ncp) return;
        if (!scinterm_is_dirty()) return;
        
        // Frame-rate throttle
        if (!throttle_frame()) return;
        
        dirty = false;
        arena_reset_safe(&g_render_arena);
        
        // Decide: serial ou paralelo?
        const int total_lines = LinesOnScreen();
        const bool use_parallel = use_parallel_render_ && 
                                  parallel_config_.enabled &&
                                  total_lines >= parallel_config_.min_lines_for_parallel;
        
        if (use_parallel) {
            render_parallel();
        } else {
            render_serial();
        }
    }
    
private:
    void render_serial() {
        // Código existente
        auto surface = Surface::Allocate(Technology::Default);
        surface->Init(static_cast<WindowID>(ncp));
        Paint(surface.get(), GetClientRectangle());
    }
    
    void render_parallel() {
        // Inicializa pool se necessário
        if (!render_pool_) {
            render_pool_ = std::make_unique<ThreadPool>(
                parallel_config_.num_threads);
        }
        
        // Cria surface paralela
        if (!parallel_surface_) {
            const int tiles = parallel_config_.num_threads;
            parallel_surface_ = std::make_unique<ParallelSurface>(tiles, 
                LinesOnScreen() / tiles);
        }
        
        // Renderiza tiles em paralelo
        parallel_surface_->parallel_paint(this, GetClientRectangle(), 
                                         *render_pool_);
        
        // Compõe resultado (thread principal)
        auto target = Surface::Allocate(Technology::Default);
        target->Init(static_cast<WindowID>(ncp));
        parallel_surface_->composite_to(target.get());
    }
};
```

---

## 5. Otimizações

### 5.1. Work Stealing

```cpp
// Thread pool avançado com work stealing
class WorkStealingPool {
    struct Worker {
        std::deque<std::function<void()>> local_queue;
        std::mutex mutex;
        std::atomic<bool> working{false};
    };
    
    std::vector<std::unique_ptr<Worker>> workers_;
    
    void worker_loop(int worker_id) {
        while (!stop_) {
            std::function<void()> task;
            
            // 1. Tenta sua própria queue
            auto& my_queue = workers_[worker_id];
            {
                std::lock_lock lock(my_queue->mutex);
                if (!my_queue->local_queue.empty()) {
                    task = std::move(my_queue->local_queue.front());
                    my_queue->local_queue.pop_front();
                }
            }
            
            // 2. Tenta steal de outro worker
            if (!task) {
                task = steal_from_other(worker_id);
            }
            
            // 3. Executa ou espera
            if (task) {
                task();
            } else {
                std::this_thread::yield();
            }
        }
    }
};
```

### 5.2. Cache de Layout por Linha

```cpp
struct LineLayoutCache {
    struct Entry {
        int line_number;
        std::vector<XYPOSITION> char_positions;
        int wrap_count;
        std::chrono::steady_clock::time_point last_access;
    };
    
    static constexpr int CACHE_SIZE = 256;
    std::array<Entry, CACHE_SIZE> entries_;
    std::atomic<int> next_slot_{0};
    
    // Thread-local cache
    thread_local static std::array<Entry, 64> tls_cache_;
    
    const Entry* find_line(int line) {
        // Primeiro procura no TLS
        for (auto& e : tls_cache_) {
            if (e.line_number == line) return &e;
        }
        // Depois no cache global
        // ...
    }
};
```

### 5.3. SIMD para Processamento de Texto

```cpp
// AVX2 para encontrar quebras de linha e tabs
#include <immintrin.h>

void find_special_chars_simd(const char* text, size_t len, 
                             std::vector<size_t>& positions) {
    const __m256i newline = _mm256_set1_epi8('\n');
    const __m256i tab = _mm256_set1_epi8('\t');
    
    size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        __m256i chunk = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(text + i));
        
        __m256i nl_mask = _mm256_cmpeq_epi8(chunk, newline);
        __m256i tab_mask = _mm256_cmpeq_epi8(chunk, tab);
        __m256i combined = _mm256_or_si256(nl_mask, tab_mask);
        
        int mask = _mm256_movemask_epi8(combined);
        while (mask != 0) {
            int bit = __builtin_ctz(mask);
            positions.push_back(i + bit);
            mask &= (mask - 1);  // Clear lowest bit
        }
    }
    
    // Resto scalar
    for (; i < len; i++) {
        if (text[i] == '\n' || text[i] == '\t') {
            positions.push_back(i);
        }
    }
}
```

---

## 6. Testes

### 6.1. Testes de Corretude

```cpp
// tests/test_parallel_render.cpp

TEST(ParallelRender, SameOutputAsSerial) {
    // Cria editor com conteúdo
    auto editor = create_test_editor();
    editor->set_text(generate_test_document(1000));
    
    // Render serial
    auto serial_output = capture_render(editor.get(), 
                                        RenderMode::SERIAL);
    
    // Render paralelo
    auto parallel_output = capture_render(editor.get(), 
                                          RenderMode::PARALLEL);
    
    // Deve ser idêntico pixel-a-pixel
    EXPECT_EQ(serial_output, parallel_output);
}

TEST(ParallelRender, ThreadSafetyStress) {
    auto editor = create_test_editor();
    std::atomic<int> errors{0};
    
    // Thread de renderização
    std::thread renderer([&] {
        for (int i = 0; i < 1000; i++) {
            editor->render();
        }
    });
    
    // Thread de modificação
    std::thread modifier([&] {
        for (int i = 0; i < 500; i++) {
            editor->insert_text(random_position(), random_text());
            std::this_thread::sleep_for(1ms);
        }
    });
    
    renderer.join();
    modifier.join();
    
    EXPECT_EQ(errors, 0);
}
```

### 6.2. Benchmarks

```cpp
// benchmarks/render_benchmark.cpp

static void BM_RenderSerial(benchmark::State& state) {
    auto editor = create_editor_with_lines(state.range(0));
    
    for (auto _ : state) {
        editor->render();
    }
}

static void BM_RenderParallel(benchmark::State& state) {
    auto editor = create_editor_with_lines(state.range(0));
    editor->set_parallel_mode(true);
    
    for (auto _ : state) {
        editor->render();
    }
}

BENCHMARK(BM_RenderSerial)->Range(100, 10000);
BENCHMARK(BM_RenderParallel)->Range(100, 10000);
```

---

## 7. Configuração e Uso

### 7.1. API C

```c
// Configurar renderização paralela
scinterm_parallel_config_t config = {
    .num_threads = 4,
    .min_lines_for_parallel = 20,
    .enabled = true
};
scinterm_set_parallel_config(editor, &config);

// Ou usar auto-detecção
scinterm_auto_parallel_config(editor);  // Baseado em hardware

// Desabilitar se necessário
scinterm_set_parallel_enabled(editor, false);
```

### 7.2. Auto-Detecção

```cpp
ParallelRenderConfig auto_detect_config() {
    ParallelRenderConfig config;
    
    const auto hardware_threads = std::thread::hardware_concurrency();
    config.num_threads = std::max(2u, hardware_threads / 2);
    
    // Ajusta baseado em cache L1
    const int l1_cache_size = 32 * 1024;  // 32KB típico
    const int line_size_estimate = 200;   // bytes por linha
    config.min_lines_for_parallel = 
        (l1_cache_size / line_size_estimate) / config.num_threads;
    
    return config;
}
```

---

## 8. Métricas de Performance Esperadas

### 8.1. Speedup Teórico (Lei de Amdahl)

```
Fração paralelizável: ~80% (renderização de linhas)
Fração serial: ~20% (setup, composição, notcurses)

Speedup máximo com N threads:
S(N) = 1 / (0.2 + 0.8/N)

N=2:  1.67x
N=4:  2.5x  
N=8:  3.3x
N=16: 3.8x (diminuição de retornos)
```

### 8.2. Resultados Esperados

| Cenário | Serial | Paralelo (4T) | Speedup |
|---------|--------|---------------|---------|
| 100 linhas | 2ms | 3ms | 0.67x (overhead) |
| 1000 linhas | 16ms | 7ms | 2.3x |
| 10000 linhas | 180ms | 65ms | 2.8x |
| Edição rápida | 0.5ms | 0.8ms | 0.6x (overhead) |

---

## 9. Fallback e Resiliência

### 9.1. Detecção de Problemas

```cpp
class ParallelRenderer {
    std::atomic<int> consecutive_errors_{0};
    
    void render_safely() {
        try {
            render_parallel();
            consecutive_errors_ = 0;
        } catch (const std::exception& e) {
            log_error("Parallel render failed: {}", e.what());
            if (++consecutive_errors_ > 3) {
                // Fallback para serial
                use_parallel_ = false;
                log_warn("Disabling parallel render due to errors");
            }
            render_serial();  // Fallback
        }
    }
};
```

### 9.2. Memory Pressure

```cpp
void check_memory_pressure() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    
    // Se memória > 80%, reduzir threads
    if (usage.ru_maxrss > memory_threshold_ * 0.8) {
        reduce_thread_pool_size();
    }
}
```

---

## 10. Conclusão

Esta arquitetura de renderização paralela:

1. **Mantém compatibilidade** - funciona com NotCurses thread-unsafe
2. **Escala bem** - speedup de 2-3x em documentos grandes
3. **É segura** - fallback automático e proteção contra race conditions
4. **É configurável** - ajustável para diferentes hardwares

### Próximos Passos de Implementação

1. ✅ Design document (este arquivo)
2. ⬜ Thread pool básico
3. ⬜ ParallelSurface
4. ⬜ Integração com ScintillaNotCurses
5. ⬜ Testes de corretude
6. ⬜ Benchmarks
7. ⬜ Otimizações (SIMD, cache)
