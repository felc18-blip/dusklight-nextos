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
