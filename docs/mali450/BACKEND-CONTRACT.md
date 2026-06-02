# Contrato de implementação — backend GLES2 (camada wgpu-compat)

Escopo medido no código do Aurora (commit base). É o checklist das próximas semanas.

## Estratégia: Route A
Camada `wgpu`-compatível sobre GLES2 (substitui `dawn::webgpu_dawn`) + forkar SÓ o
gerador de shader (`gx/shader.cpp`) pra emitir GLSL ES 1.00. Assim `gfx/*`, `gx/pipeline.cpp`
e `dolphin/gx/*` compilam quase sem mudança.

## Superfície de API a implementar (~30 métodos, contagem de uso)

### Device.Create*
- CreateBuffer(5) · CreateTexture(9) · CreateView(9) · CreateSampler(6)
- CreateShaderModule(7) — recebe **GLSL ES** (após fork do gerador), não WGSL
- CreateBindGroupLayout(9) · CreateBindGroup(9) · CreatePipelineLayout(7)
- CreateRenderPipeline(6) — mapeia descriptor → programa GLES2 + vetor de estado
- CreateComputePipeline(1) — **só depth_peek; stubar** (ver abaixo)
- CreateSurface(1)

### Queue
- WriteBuffer(2) → glBufferSubData · WriteTexture(2) → glTexSubImage2D · Submit (flush)

### CommandEncoder
- BeginRenderPass(4) → bind FBO + set viewport/clear
- BeginComputePass(1) → **só depth_peek; stubar**
- CopyBufferToBuffer(2) · CopyBufferToTexture(1) · Finish

### RenderPassEncoder
- SetPipeline(5) → glUseProgram + aplica estado · SetBindGroup(8) → bind tex/uniforms
- SetViewport(3) · SetScissorRect(2) · SetBlendConstant(2) · SetIndexBuffer(1)
- Draw(4) → glDrawArrays · DrawIndexed(1) → glDrawElements · End

### Buffer (mapping — GLES2 não tem)
- MapAsync(2) · GetMappedRange(1) · Unmap(2) → emular com memória cliente + glBufferSubData

### Callbacks (stubs triviais)
- SetUncapturedErrorCallback · SetLoggingCallback · SetDeviceLostCallback

## Casos especiais (as 3 paredes)

1. **Vertex pulling → attributes.** `gx/shader.cpp:1819-1821` declara `var<storage,read> vbuf/abuf`.
   `gfx/common.cpp:512,521` cria os bind groups `ReadOnlyStorage`. No GLES2 (Utgard sem vertex
   texture fetch) isso vira VBO + glVertexAttribPointer. **Tocar: common.cpp + shader.cpp.**
   Mapa de referência: `pc_gx.c` do AC (PC_GX_MAX_VERTS).

2. **Uniform buffer → glUniform.** `var<uniform> ubuf: Uniform{...}` → uniform arrays + glUniform*.
   Limites medir no device (GL_MAX_VERTEX_UNIFORM_VECTORS; no Utgard ~128).

3. **WGSL → GLSL ES 1.00.** Fork de `gx/shader.cpp`. Helpers de overflow do TEV já são float (bom).
   Sem inteiros/bitwise → mod/floor/step. `precision mediump float`. Über-shader subset + fallback.

## Stubs iniciais (renderiza sem, adiciona depois)
- `depth_peek.cpp` (compute) → retorna profundidade dummy; perde oclusão de sol/lens flare. F4.
- MSAA/anisotropy → desligar no Utgard.

## Ordem de ataque
1. Header `wgpu` compat mínimo (enums + handles) — substitui webgpu_cpp.h no caminho GLES2.
2. Device/Queue/Buffer/Texture básicos + clear (liga com lib/gles2/context).
3. Pipeline + ShaderModule (forkar gerador GLSL ES) — primeiro triângulo do GX.
4. BindGroup + vertex attributes (a parede 1) — primeira geometria TP.
5. Texturas + TEV subset — primeira sala.
6. EFB copies, depth_peek real, indirect — efeitos.

## Fidelidade PC=device
Build no PC contra o SDL3 fork `felc18-blip/SDL3-mali-fbdev` + GLES2 via Mesa + GLSL ES 1.00,
disciplinado ao subconjunto Utgard. O que renderiza no PC mapeia pro Mali-450.

### IMPORTANTE: device NÃO tem Mesa (só blob libMali via fbdev)
PC=Mesa, device=blob proprietário. Mesmo padrão (EGL1.5/GLES2/GLSL-ES-1.00), implementações
diferentes. O blob é MAIS rígido (limite ~512 instr Utgard, quirks mediump, extensões próprias).
Estratégia:
- **Mesa estrito no PC:** exportar `MESA_GLES_VERSION_OVERRIDE=2.0` e `MESA_GLSL_VERSION_OVERRIDE=100`
  pra Mesa rejeitar GLES3-ismos (aproxima do blob).
- **Disciplina:** `#version 100`, `precision mediump`, sem GLES3, sem vertex texture fetch, sem
  derivatives sem extensão, sem `texture*Lod` no fragment sem extensão.
- **Juiz final = device:** validar shaders no blob real periodicamente (SDL3 fbdev já funciona).
  Mandar mini-programa de teste por SSH; o blob decide o que compila e cabe nos 512.
- Opcional: ARM Mali Offline Compiler pra contar instruções offline.
Mesa é só pra ITERAR rápido (estrutura/lógica); o blob é a VERDADE.
