# Design do backend GLES2 no Aurora — achados de código (2026-06-02)

## Como o Aurora está acoplado ao WebGPU (medido)

Os tipos `wgpu::` vazam por todo `lib/gx` e `lib/gfx`, MAS a maioria é **enum/descriptor (POD)**, não comportamento:
- `gfx/common.cpp` = núcleo (38 tipos wgpu, 84 ocorrências) — a abstração central.
- `gfx/texture.cpp` = 12 tipos, 52 ocorrências.
- `gx/pipeline.cpp`, `gx.hpp`, `GXFrameBuffer.cpp` = baixo acoplamento (1-4 tipos).

**Objetos wgpu com comportamento real a implementar em GLES2 (~15):**
Device, Queue, Buffer, Texture, TextureView, Sampler, RenderPipeline, BindGroup,
BindGroupLayout, PipelineLayout, ShaderModule, CommandEncoder, RenderPassEncoder, Surface.
O resto (TextureFormat, BlendFactor, BufferUsage, CompareFunction, FilterMode...) são enums → mapear pra GL.

## Estratégia escolhida: wgpu-compat fino sobre GLES2 + gerador GLSL ES 1.00

Em vez de reescrever gx/gfx inteiros, criar uma **camada `wgpu` compatível, backed por GLES2** (namespace/headers que fornecem os enums como plain enums e os ~15 objetos como handles GLES2). Assim `gx/pipeline.cpp` e `gfx/*` compilam quase sem mudança. Pipeline descriptor → programa GLES2 + vetor de estado; bind group → bindings de textura/uniform; Command/RenderPassEncoder → chamadas GLES2 imediatas.

## As 3 reescritas DURAS (o trabalho real)

1. **Vertex pulling → vertex attributes clássicos.**
   WGSL usa `@group(0) var<storage,read> vbuf/abuf: array<u32>` e o VS puxa por `vidx`.
   **Utgard (Mali-450) NÃO tem vertex texture fetch** → não dá pra emular storage buffer com textura no VS.
   → Materializar VBOs reais (glVertexAttribPointer) no caminho de submissão de vértice (gx.cpp / streaming buffer). O `pc_gx.c` do AC já faz exatamente isso (PC_GX_MAX_VERTS) — reaproveitar.

2. **Uniform buffer → glUniform individuais.**
   WGSL `var<uniform> ubuf: Uniform { vtx_start, current_pnmtx, viewport sizes, array_start: array<u32,12>, ... }`.
   GLES2 não tem UBO → uniform arrays (mat/vec) + glUniform*. Conferir GL_MAX_VERTEX_UNIFORM_VECTORS no blob (GX usa ~10 pos/nrm mtx + tex mtx).

3. **WGSL → GLSL ES 1.00 (`#version 100`, float-only).**
   Reescrever a montagem de string em `gx/shader.cpp` (~1758 linhas).
   - Sem inteiros/bitwise: máscaras TEV viram mod/floor/step. **BOM:** os helpers de overflow do TEV JÁ são float (`floor(x/256)*256`) → traduzem direto.
   - `precision mediump float;` no topo (igual `default.frag` do AC).
   - até 16 estágios TEV + KColors + indirect podem estourar ~512 instr do Utgard → über-shader subset + fallback gerado/cacheado pros pesados; aproximar/cortar.

Extra: **EFB→textura** (bloom/twilight/DoF) = FBO + glReadPixels/blit. AC já tem `s_efb_captures` como base.

## Notícias boas descobertas no build

- O build do Aurora pra `TARGET_PC` JÁ liga flags: `-DDAWN_ENABLE_BACKEND_OPENGLES`, `-DSDL_VIDEO_DRIVER_MALI=1`, `-DEGL_API_FB=1`, `-DDAWN_ENABLE_BACKEND_DESKTOP_GL`. Ou seja, o projeto já contempla Mali/EGL-fbdev/GLES no toolchain — investigar se há caminho parcial reaproveitável.
- **SDL3 fbdev de vocês já funciona no Amlogic-old** (fork `felc18-blip/SDL3-mali-fbdev`, branch mali-350-improvements / SDL 3.5.0 main, commit e8e2872, driver mali-fbdev intacto). App layer SDL3 do Aurora é viável no device → de-risca F6. Usar ESSE SDL3 no device (e no PC pra alinhar).
- Dawn e SDL3 vêm **prebuilt** (download), não compila do zero → build rápido.

## Setup de build no PC (funcionando)

- Repo: `dusklight/` + submódulo `extern/aurora` (sem recursar Dawn; vem prebuilt).
- Toolchain Arch: cmake 4.3, ninja, clang 22, python 3.14, Vulkan 1.4 (RTX 4070).
- Preset: `linux-clang`. Build dir: `build/linux-clang`.
- **Fix necessário:** SDL3 prebuilt do Aurora (ref 29f7f08) é velha demais (falta `SDL_EVENT_FINGER_CANCELED` em rmlui.cpp). Solução: `-DAURORA_SDL3_PROVIDER=system` (Arch sdl3 3.4.8 tem o símbolo). No device usar o fork mali-fbdev.

## Plano de contexto GLES2 no PC (pra desenvolver/ver na tela aqui)

Pedir contexto **GLES2 real** (não GL desktop) via EGL/Mesa no PC, pra que o que funciona aqui mapeie pro Mali-450. Mesa fornece GLES2 no desktop. Assim F1-F5 iteram no PC com fidelidade ao alvo.
