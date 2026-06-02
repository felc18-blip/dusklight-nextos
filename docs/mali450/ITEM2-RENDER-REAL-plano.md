# Item 2 — Render real (geometria na tela). Plano executável.

Estado: **etapa 1 iniciada** (`lib/gx/shader_gles2.cpp` criado, compila no aurora_gx).
Item 1 (apresentacao na janela) JA funciona e e visivel (azul na tela).

## Costura (como liga tudo)
1. `gx/pipeline.cpp:13` chama `build_shader(config.shaderConfig)` -> hoje gera WGSL.
   Sob `AURORA_GLES2`, fazer chamar `build_shader_gles2(config)` -> {vert, frag} GLSL ES.
2. Rotear o GLSL pro shader module: o jeito mais simples e empacotar `vert\n---\nfrag`
   no campo `code` do WGSL descriptor e `wgpuDeviceCreateShaderModule` (compat) le e
   separa pros campos `WGPUShaderModuleImpl::vs/fs`. (Ou um canal lateral.)
3. `wgpuDeviceCreateRenderPipeline` (compat): compila vs+fs num programa GL
   (glCreateProgram/Shader/CompileShader/LinkProgram), guarda em
   `WGPURenderPipelineImpl::program` + resolve estado (blend/depth/cull do descriptor).
4. `RenderPassEncoder` (compat): SetPipeline=glUseProgram+estado; SetVertexBuffer=bind VBO+
   glVertexAttribPointer (layout vem do pipeline vertex state); SetBindGroup=bind texturas+
   glUniform; Draw=glDrawArrays; DrawIndexed=glDrawElements.

## As 3 paredes (ordem de ataque, cada uma um commit + teste na tela)

### Etapa 1 (FEITO o esqueleto): caso base
- VS: `gl_Position = u_mvp * vec4(a_position,1)`, passa `a_color0`.
- FS: cor + alpha test.
- Falta: derivar `u_mvp` do GX (matrizes pos/proj do uniform) e ligar attribs reais.
- Meta visual: o fade do JUTFader (quad colorido) aparece -> tela deixa de ser so azul.

### ⚠️ DESCOBERTA CRITICA sobre o formato de vertice (attr_load, shader.cpp:632)
O vertex pulling do Aurora e MAIS complexo que "ler attribs inline":
1. **Atributos indexados** (GX_INDEX8/16): vbuf guarda um INDICE; o dado real (pos/nrm)
   vive em `abuf` (array buffer) em `array_start[attr] + indice*stride`. Indirecao dos
   vertex arrays do GameCube. NAO mapeia direto pra glVertexAttribPointer.
2. **Big-endian**: "Vertex buffer is always big endian". Attributes GL = little-endian.
3. Formatos variados (s16/f32/u8 norm), cnt 2 ou 3 (pos pode ser vec2), cores empacotadas.
**Consequencia**: pra GLES2, montar na CPU um VBO PLANO (de-indexado) + BYTE-SWAP, num
formato fixo (ex: pos vec3 f32 + cor vec4 u8). Fazer isso no ponto onde o draw eh submetido
(handle_draw_unmerged / GXVert), tendo acesso a vbuf+abuf+array_start. Indices viram um
IBO uint16 (ja existe g_indexBuffer). Buffers GL reais (glGenBuffers/glBufferData), nao so
cpu mirror.

### DESIGN PRONTO-PRA-CODAR do conversor de vertices (tudo levantado)
Dados/estruturas (confirmado no codigo):
- `DrawData` (pipeline.hpp): vertRange, idxRange, uniformRange, vtxCount, indexCount,
  instanceCount, bindGroups, dstAlpha. (NAO carrega attrs -> converter onde config existe.)
- `AttrConfig` (gx.hpp:436): attrType(GX_NONE/DIRECT/INDEX8/INDEX16), cnt, compType(GXCompType),
  offset(no vertice), stride(do array), frac, le.
- Buffers (gfx/common.cpp, static): `g_verts`(vbuf), `g_storage`(abuf arrays), `g_indices`.
  Base: `g_verts.data()+vertRange.offset`. Arrays indexados em g_storage via vaRanges
  (handle_draw_unmerged:1616 push_storage). Indices uint16 em g_indices (idxRange).
- Stride do vertice = config.shaderConfig.vtxStride.

Ponto de implementacao: **`handle_draw_unmerged`** (command_processor.cpp:1593) — config+vertRange
+arrays disponiveis. Algoritmo (sob AURORA_GLES2):
1. Pra cada um dos vtxCount vertices (base = g_verts+vertRange.offset + v*vtxStride):
   - POS (GX_VA_POS): se DIRECT, ler em +attrs[POS].offset (cnt floats, big-endian -> swap).
     Se INDEX8/16, ler indice, buscar em g_storage[vaRange + idx*attrs.stride].
   - CLR0: idem (cor u8x4 ou packed). Default branco se ausente.
   - escrever flat: [px,py,pz (f32 LE), r,g,b,a (f32 LE)] interleaved (stride 28).
2. glGenBuffers VBO + glBufferData(flat). Guardar handle+vtxCount em DrawData (campos novos
   sob AURORA_GLES2). Indices: criar IBO uint16 de g_indices[idxRange].
3. Em `render()` (sob AURORA_GLES2): glUseProgram (ja via SetPipeline), bind VBO/IBO,
   glVertexAttribPointer(0=pos vec3, 1=col vec4), setar u_mvp (ver abaixo), glDrawElements.

Helpers de endian: `read_u16/read_f32 big-endian` ja existem (command_processor.cpp usa
read_u16). Reusar ou escrever swap simples.

**u_mvp**: a matriz model-view-proj do vertice esta no uniform buffer (build_uniform,
shader_info). Etapa 2a: derivar a matriz combinada (pos mtx * proj). Conferir layout no
gerador WGSL (vtxXfrAttrs) e em build_uniform. Primeiro teste: passar identidade ou ortho
2D pra validar o pipeline com a UI 2D, depois a matriz real pro 3D.

GL real nos buffers: hoje os WGPUBuffer sao so cpu mirror. Pro draw, precisa de VBO/IBO GL
de verdade (glGenBuffers/glBufferData) — fazer no converter (acima), nao depender do mirror.

### Etapa 2: vertex attributes (parede 1)
- O VS WGSL le `vbuf` (storage) via `attr_load(config, attr, vidx)` (shader.cpp:632).
  No GLES2: declarar `attribute` por attr ativo (config.attrs) e bindar VBO real.
- Em `begin_frame`, `g_verts` recebe os vertices empacotados (stride=config.vtxStride).
  Em SetVertexBuffer, fazer glVertexAttribPointer por attr usando offset/format do GX.
- Cuidado: formatos GX (s16/f32/u8 norm) -> mapear pro glVertexAttribPointer (type+normalized).

### Etapa 3: TEV em GLSL ES (parede 3)
- Traduzir os ate 16 estagios (config.tevStages) -> ops float no FS.
  Reusar os helpers de overflow (ja float em shader.cpp:1790) e o subset do AC (default.frag).
- KColors/regs -> uniforms. Texturas -> sampler2D + texture2D (nao textureSampleBias).
- alphaCompare completo. Estouro de 512 instr: medir; usar uber-shader subset + fallback.

### Etapa 4: texturas + lighting + fog
- Upload de textura (wgpuQueueWriteTexture -> glTexImage2D/glTexSubImage2D), formatos GX.
- Lighting per-vertex (config.colorChannels + lighting_func shader.cpp:695) no VS.
- Fog no FS.

### Etapa 5: EFB copies / indirect (item F4-F5)
- FBO + glReadPixels pros efeitos de tela; indirect aproximado.

## Disciplina (sempre)
`#version 100`, `precision mediump float`, sem inteiros/bitwise (mod/floor/step),
sem vertex texture fetch, sem UBO/storage. Testar com `MESA_GLES_VERSION_OVERRIDE=2.0`.
Juiz final = blob Mali no device.

## Referencias no codigo
- gerador WGSL a portar: `lib/gx/shader.cpp` (attr_load 632, lighting_func 695, build_shader 786, template 1790-1855).
- vertex streaming: `lib/gfx/common.cpp` (begin_frame 641, map_uniform 1019).
- shim AC (mapa do TEV em GLSL ES): `REFERENCIA-AC-SHIM/` (pc_gx.c, default.frag.v3-FINAL).
