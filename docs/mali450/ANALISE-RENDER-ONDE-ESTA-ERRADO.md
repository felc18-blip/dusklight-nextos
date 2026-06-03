# Análise: onde o render está errado (mapa completo) — 2026-06-02

Estado: a intro de TP renderiza reconhecível (geometria+matriz OK), mas terreno chapado,
cores sem detalhe, frames pretos no boot, ~1.2fps. Mapeei a causa de cada um.

## 1. TEXTURA CHAPADA / UV errada — CAUSA RAIZ: falta TexGen (matriz de textura)
**O texcoord usado pra samplear NÃO é o texcoord cru do vértice.** O GX gera via TexGen
(shader.cpp:1111-1151):
- Fonte (`tcg.src`): pode ser TEX0-7 (texcoord do vértice) OU **POS/NRM/COLOR** (gerado).
- **Transformado por matriz de textura**: `tc = source * postex_mtx[tcg.mtx/3]` (type GX_TG_MTX2x4/3x4).
- normalize + postMtx opcionais.

Eu uso o **texcoord cru (TEX0) direto, sem a matriz** → escala/tiling errado → UVs gigantes
(96.0) ou chapadas. **Esse é o bug das texturas.**

Dados (já existem):
- `TcgConfig` (gx.hpp:155): type, src, mtx(GXTexMtx), postMtx, normalize.
- Matrizes de textura: `g_gxState.texMtxs` (Mat3x4<float>[MaxTexMtx]).
- No uniform (build_uniform): postex_mtx = pnMtx.pos[MaxPnMtx] **seguido de** texMtxs[MaxTexMtx].
  Índice da texMtx no array postex_mtx = `MaxPnMtx + tcg.mtx/3`? NÃO — shader usa
  `postex_mtx[texMtxIdx]` com `texMtxIdx = tcg.mtx/3` direto (as pos mtx ocupam 0..9, tex 10..).
  Conferir: build_uniform escreve pnMtx[i].pos (10) e DEPOIS texMtxs (10) -> postex_mtx[0..9]=pos,
  [10..19]=tex. E shader.cpp:1136 `texMtxIdx = tcg.mtx/3`. GXTexMtx GX_TEXMTX0=30,30/3=10 -> bate.

**FIX (prioridade 1):** snapshot do tcg[0] (src/type/mtx/normalize) + a texMtx (Mat3x4) no
DrawData; no render(), se src==POS usar a posição como tc, senão o texcoord; multiplicar pela
texMtx (mat3x4: tc.xy' = dot(row,(tc.x,tc.y,?,1))). Aplicar a matriz resolve o tiling correto.

## 2. COR SEM DETALHE — falta TEV (raster color + estágios)
Eu emito `gl_FragColor = texture2D(u_tex0, uv)`. O GX combina via **TEV** (shader.cpp:375,995):
cor final = estágios combinando textura, **raster (cor de vértice/material)**, konstants, com
op/bias/scale/clamp. Falta: modular pela cor raster (material/vertex), multi-estágio, kcolors.
**FIX (prioridade 2):** ao menos `texture * raster_color` (raster = cor de canal/material). Já
leio a cor de vértice (uso branco fixo); usar a cor real + modular pela textura aproxima muito.

## 3. FRAMES PRETOS — boot/loading (provável legítimo)
Frames 20/40/70 pretos = telas de boot/loading. Frame 110+ = intro. Não é bug.

## 4. PERFORMANCE ~1.2fps — F7
- glBufferData(STREAM) orphan por draw + conversão CPU por vértice (std::vector).
- glGetUniformLocation por draw (JÁ cacheado).
- Mesa-zink (GL-on-Vulkan) no PC não é representativo do blob Mali.
**FIX (depois):** buffer persistente único por frame (concatenar draws), VAO-less attribs uma vez.

## ATUALIZAÇÃO (após estudo das texturas)
- **Texturas uploadam CORRETAMENTE**: dump da textura gl35 mostrou detalhe (fogo laranja).
  Upload via WriteTexture (UseTextureBuffer=false) funciona. tex_gl1/2/4 brancas = UI/font.
- **TexGen do terreno = IDENTITY** (texMtxId=1, tcgSrc=4=TEX0): usa texcoord cru. Logo TexGen
  não era a causa do terreno. UVs razoáveis (tiling 1-15x) exceto tex0=36 (96x, frac=8).
- Mipmaps + cor de vértice (texture*v_color) implementados — **não mudaram a área grande chapada**.
- **CONCLUSÃO**: as grandes regiões oliva/marrom (céu/fundo/terreno) são polígonos cuja cor/
  detalhe real vem do **TEV completo (multi-estágio) + iluminação + material color (uniform)**,
  que eu ainda não replico -> caem na cor base. NÃO é bug de upload/UV. É a profundidade do TEV.
- **Performance: 30fps SOLIDOS** (medido). Perf NAO e problema. Otimo sinal pro device.
  glBufferData/conversão. No blob Mali nativo seria diferente. F7.

## O QUE FALTA REALMENTE (ordem de impacto, mas TODOS profundos)
1. **TEV multi-estágio** (shader.cpp:375,995): combinar textura(s) + raster + kcolors + ops.
   É o 1758-linhas do gerador WGSL portado pra GLSL ES. Maior trabalho restante.
2. **Material/channel color** (uniform matColor/ambColor) — cor das superfícies sem vertex color.
3. **Lighting** (per-vertex, colorChannels) — sombreamento.
4. **Performance** (buffer único por frame; provavelmente OK no device).
5. Múltiplas texturas (tex1..7), indirect, fog.

## Prioridade de ataque
1. **TexGen (matriz de textura)** -> terreno/texturas mapeiam certo (maior ganho visual).
2. **TEV raster color** -> tonalidade correta.
3. Performance.
4. Mipmaps (anti-alias do tiling).

## Ferramentas
Captura: dump glReadPixels->PPM no present (frame N). Converter `magick f.ppm f.png`. Eu vejo
via Read da imagem. Logs de UV/tex já instrumentados em pipeline.cpp render().
