# Dusklight (Twilight Princess) → Mali-450 GLES2 / Amlogic-old / 1GB

**Abordagem decidida: FORK, no código, backend GLES2 NATIVO dentro do Aurora. SEM shim (sem LD_PRELOAD).**

Data do plano: 2026-06-02. Workspace: `TRABALHO CLAUDE CODE/DUSKLIGHT-MALI450-PORT/`

---

## 0. O que já está clonado aqui

| Repo | Onde | Pra quê |
|---|---|---|
| `TwilitRealm/dusklight` | `dusklight/` (186M) | o jogo completo jogável (decomp TP + runtime + glue) |
| `encounter/aurora` | `dusklight/extern/aurora/` | a ENGINE — é aqui que entra o backend GLES2 |
| `zeldaret/tp` | `tp/` (319M) | decomp TP upstream — REFERÊNCIA pra entender uso de GX |
| AC shim (seu) | `REFERENCIA-AC-SHIM/` | prova de conceito GX→GLES2 que VOCÊ já fez funcionar |

---

## 1. Decisão de arquitetura (sem shim, forkado)

O Dusklight roda sobre o **Aurora**, cuja camada gráfica (GX→tela) é construída em **WebGPU (Dawn)** com backends **só D3D12 / Vulkan / Metal**. Mali-450 (Utgard) não tem Vulkan nem GLES3 → o caminho atual NÃO roda.

**Plano:** adicionar dentro do Aurora um **backend GLES2 nativo** (módulo novo `lib/gles2/`), implementando a mesma interface interna que o `lib/webgpu/gpu.cpp` implementa, e um **gerador de GLSL ES 1.00** paralelo ao `lib/gx/shader.cpp`. Seleção do backend em tempo de build (CMake) ou runtime.

**Por que no código e não shim:**
- Reaproveitável pra TODO jogo Aurora (AC-via-Aurora, Metroid Prime/Metaforce...), não só TP.
- Sem a fragilidade de interceptar símbolos GX por LD_PRELOAD.
- **Remove o Dawn inteiro** da build → binário menor, menos RAM, viável em 32-bit.
- Fica um fork limpo, versionável, com PRs internos.

---

## 2. Mapa do código a tocar (tamanhos reais medidos)

Renderer Aurora total (gfx+gx+webgpu) = **~14.491 linhas**. Núcleo a reescrever/espelhar:

| Arquivo | Linhas | O que faz | Ação GLES2 |
|---|---|---|---|
| `lib/webgpu/gpu.cpp` | ~900 | cria device/queue/pipelines/bind groups WebGPU | **espelhar** em `lib/gles2/gpu_gles2.cpp` (contexto EGL+GLES2, FBOs, glProgram) |
| `lib/gx/shader.cpp` | ~1758 | gera shader **WGSL** a partir da config TEV/indirect/luz | **reescrever gerador** pra GLSL ES 1.00 (`shader_gles2.cpp`) |
| `lib/gx/pipeline.cpp` | — | monta render pipeline (blend/depth/cull) | mapear estados → glEnable/glBlendFunc/glDepth |
| `lib/gfx/*` (clear, texture, tex_copy_conv, depth_peek, common) | ~grande | clear, upload textura, EFB copy, depth peek | adaptar p/ FBO/glTexImage2D/glReadPixels |
| `lib/dawn/*`, `lib/rmlui/WebGPURenderInterface` | — | binding Dawn + UI WebGPU | substituir/stubar (UI ImGui/RmlUi em GLES2) |
| `lib/aurora.cpp` | — | seleção de backend, init janela SDL3 | adicionar branch GLES2/EGL |

A lógica de GX (TEV/transform/lighting em `lib/dolphin/gx/*` e `lib/gx/gx.cpp`) **não muda** — ela só alimenta a config; quem consome é o gerador de shader e o gpu backend.

---

## 3. As 5 PAREDES técnicas do GLES2 (Utgard) e como vencer cada uma

O Aurora usa padrões da era WebGPU/Vulkan que **não existem** em GLSL ES 1.00 (`#version 100`). Confirmado lendo `gx/shader.cpp`:

1. **Vertex pulling por storage buffer** — WGSL usa `var<storage,read> vbuf: array<u32>`. GLES2 não tem storage buffer.
   → **Reconstruir VBOs clássicos** (glVertexAttribPointer). Seu `pc_gx.c` já faz isso (PC_GX_MAX_VERTS) — reaproveitar a lógica.

2. **Uniform buffer (`var<uniform> ubuf`)** — GLES2 não tem UBO.
   → trocar por **glUniform individuais** (igual seu `default.frag`: u_mat_color, u_kcolor[4], u_tev*_in...).

3. **Sem inteiros / bitwise** — o gerador usa `f32(u32(x) & 0xFu)` (máscaras TEV), `u32`, `i32`. GLSL ES 1.00 é float-only.
   → emular máscara/seleção com **mod/floor/step** em float. (Seu shader já é `precision mediump float` puro — mesma filosofia.)

4. **Estágios TEV (até 16) + KColors + indirect** podem **estourar o limite ~512 instruções do Utgard**.
   → estratégia **über-shader dirigido por uniforms** (subconjunto TEV, como o seu AC) PARA materiais comuns + **fallback de shader gerado/cacheado** só pros materiais pesados; aproximar/cortar efeitos que não cabem. Medir contagem por material.

5. **Indirect texturing** (água, calor, bump, efeito "crepúsculo") — `textureSampleBias` + matrizes indiretas.
   → emular com lookups extra de textura no fragment. Caro; candidato a aproximação. AC **não usa** isso — é território novo.

Extra: **EFB→textura** (bloom/DoF/dessaturação do twilight) = FBO + glReadPixels/blit. Seu AC já tem `s_efb_captures` — base existe, mas TP usa muito mais.

---

## 4. Fases (milestones) — do "boota" ao "joga"

- **F0 – Build host primeiro.** Compilar Dusklight no PC com o backend GLES2 NOVO rodando sobre **EGL+GLES2 desktop (ou via ANGLE→GLES)**. Iterar rápido no PC antes do device. *(Sem device no loop = ciclo de minutos, não horas.)*
- **F1 – Janela + clear + 1 triângulo** pelo backend GLES2 (prova o caminho device/FBO/swap).
- **F2 – Geometria estática + 1 textura + TEV mínimo** (über-shader subset). Meta: menu/tela de título renderiza.
- **F3 – TEV completo (subset grande) + lighting + fog + alpha test.** Meta: uma sala interior de TP renderiza correta.
- **F4 – EFB copies + efeitos de tela** (bloom/twilight) na medida do que couber no Utgard.
- **F5 – Indirect texturing** (água/calor) — aproximado.
- **F6 – Port no device:** cross-compile **32-bit armhf**, empacotar PortMaster (igual seu AC .sh), gptokeyb, testar no S905W2.
- **F7 – Performance pass:** medir FPS por área; cortar/baixar efeitos; LOD; resolução interna.

Cada fase é um commit/PR no fork. F0–F5 no PC; só F6+ no device.

---

## 5. O QUE PRECISO DE VOCÊ (device/toolchain) — pra fechar 100% o plano

1. **Toolchain 32-bit do Amlogic-old:** como o AC decomp foi compilado pro device? (cross-compiler armhf, sysroot, CMake toolchain file?) O `.sh` mostra `PORT_32BIT="Y"` e `libs.${DEVICE_ARCH}` — preciso do toolchain/sysroot exato.
2. **Confirmar GL blob:** Mali-450 no S905W2 = **GLES2 32-bit (armhf)**, certo? Tem EGL fbdev/gbm? Qual lib (`libMali`/`libGLESv2`) e versão?
3. **SDL no device:** o Aurora usa **SDL3**. O Amlogic-old tem SDL3? (ou só SDL2?) — define se mantemos a app layer do Aurora ou trocamos por SDL2.
4. **RAM/swap:** 1GB + quanto de swap? (TP working set + driver GL).
5. **Acesso ao device de teste:** S905W2 em `192.168.31.197` (root/emuelec) ainda vale pra F6?
6. **Assets/ISO:** você tem o dump GameCube de TP (USA/EUR) pra testar? (Dusklight não traz asset.)
7. **Onde mora o fork:** crio `felc18-blip/dusklight-nextos` + `felc18-blip/aurora-nextos`? (mesma convenção `-nextos`).

Com esses 7 respondidos, o plano vira executável fase a fase.

---

## 6. Veredito de viabilidade (honesto)

- **Renderizar GameCube em Mali-450 GLES2 é possível** — você JÁ provou com o AC.
- TP é **~5–10× a superfície de GX** do AC (16 TEV, indirect, EFB effects) → trabalho **grande** (ordem de meses), concentrado no gerador de shader e no gpu backend (~2.600 linhas-núcleo a espelhar/reescrever + adaptações em gfx).
- **Maior incerteza não é renderizar, é performance:** 4×A53 + Mali-450 + 1GB. Interiores/masmorras: plausível jogável. Campo de Hyrule aberto + efeitos: risco de slideshow → F7 corta efeitos/resolução.
- **Resultado realista:** boota, renderiza, partes jogáveis, partes capengas. "Mesmo demorando" — sim, dá pra perseguir.
