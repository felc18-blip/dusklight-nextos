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

---
## INVESTIGACAO VERTICE (2026-06-02/03) — endianness + staleness dos arrays
Sintoma: terreno chapado + "leque" (vertices voando ao infinito). Instrumentei stats por-draw
(AURORA_GLES2_VTXLOG) + dump cru de bytes (AURORA_GLES2_CAP).

ACHADOS:
1. **Draws grandes (terreno) usam POS F32 indexado** (cmp=4, INDEX16, arrStride=12). Modelos
   pequenos usam S16 (cmp=3, frac=9, ±64 local). 
2. **Staleness**: g_storage/g_verts mapeiam a memoria do STAGING buffer da GPU, DESMAPEADA no
   end_frame ANTES de gfx::render(). Ler o ponteiro do array do jogo (ou g_storage) no render =
   dados sobrescritos/stale. FIX: gles2_copy_array() copia cada array indexado pra um std::vector
   HEAP no record-time (dedup por ponteiro/frame, reset no begin_frame). Sobrevive ao unmap.
3. **Endianness**: com a copia estavel, F32 lido BE = lixo 1e38; lido LE = sao (34151, coords de
   mundo). S16 lido BE = sao (±64). => arrays F32 little-endian, inteiros big-endian.
4. **PORÉM**: decodificar F32 em LE (coords sas) => TELA PRETA consistente (3 runs). BE (F32 vira
   lixo off-screen) => terreno S16 visivel + leque. Pular as draws F32 (com LE) NAO devolve o
   terreno => nao e o plano F32 cobrindo. Provavel: as coords F32 enormes (34151, span 100k) nao
   mapeiam na tela com o modelview atual — questao de PROJECAO/MODELVIEW por-draw do terreno
   (talvez o terreno use world-space sem o pnMtx que aplico, ou precisa de outra matriz).

ESTADO ESTAVEL ESCOLHIDO: BE p/ tudo (le=false) — mostra o terreno (modelos S16). Infra de
copia-heap mantida (correta, sem stale). 

PROXIMO PASSO (sessao futura, idealmente em ambiente de GL estavel — o driver NVIDIA do PC
crasha aleatoriamente, polui o debug por captura):
- Investigar a MVP/modelview das draws F32 (terreno world-space). Conferir se currentPnMtx e o
  z-adjust (z'=w-z) batem p/ coords grandes. Talvez o terreno precise de modelview identidade +
  so a proj, ou de uma matriz de mundo especifica.
- Depois disso: F32=LE deve render o terreno F32 corretamente.

---
## BREAKTHROUGH 2026-06-03 — profundidade GC->GLES2 (era a causa da tela preta!)
Com a copia heap confiavel pude logar clip-space: **NDC z ≈ 1.00 pra TODOS os vertices**.
Causa: o z-adjust `z' = w - z` e a conversao GC->**WebGPU** (profundidade [0,1], usada pelo
WGSL/Dawn). Mas GLES2 usa NDC z em **[-1,1]**. Com w-z, geometria perto -> NDC z=2 -> CLIPADA
no far plane; tudo comprime em [1,2] e some. (No build BE so o lixo F32 esticado, com z fora
disso, escapava -> a "terreno tan" era ARTEFATO, nao geometria real.)
**FIX**: `gl_clip_z = 2*z_orig + w` (mapeia GC NDC [-1,0] -> GL [-1,1]). 
+ F32 arrays = little-endian (decode `le=(compType==4)`).
RESULTADO: geometria REAL (chao marrom) renderiza na POSICAO CORRETA. Confirmado frame d0690.

## ESTADO e PROXIMO PASSO
- Cena ainda PARCIAL (chao aparece, resto preto na maioria dos frames).
- Hipotese forte do que falta: **PNMTXIDX por-vertice**. Diagnostico mostrou que os MODELOS
  (S16) tem glHasPnMtxIdx=1 (usam indice de matriz por-vertice) mas eu aplico currentPnMtx ->
  eles mapeiam errado/fora da tela. O chao (F32, sem pnmtxidx, currentPnMtx) renderiza certo.
- Implementar per-vertex matrix: snapshot dos 10 pnMtx[].pos (+proj) por-draw num buffer estavel;
  no render, por vertice ler o indice (attr GX_VA_PNMTXIDX) e usar pnMtx[idx] no lugar da MVP fixa.

## ESTADO APOS z-fix + pnmtxidx (2026-06-03)
- Chao/terreno F32 renderiza na POSICAO CORRETA, faixa completa (frame 2100, mean 0.11).
- pnmtxidx por-vertice implementado (10 MVPs no heap, pre-transform CPU->NDC + u_mvp=identidade).
  Nao mudou frames capturados (provavel: modelos aparecem em frames que nao capturei / cena ainda
  carregando). Infra pronta.
- FALTA AINDA: (1) textura nas superficies (chapadas — TEV/UV nessas draws), (2) ceu preto (geom
  de ceu nao renderiza), (3) confirmar modelos/personagens.
- AMBIENTE: driver NVIDIA do PC crasha (RC=139) e a captura pega muito frame preto (loading/fade).
  Verificacao visual fica dificil. Ideal proximo: GL estavel ou device Mali real.

## 🏰 CENA RECONHECIVEL NO MALI-450 REAL (2026-06-03)
Castelo de Hyrule + Cidadela (rua/predios texturizados) + estruturas laranjas renderizam no
device! 4 bugs que SO aparecem no Mali real (no PC/zink o depth/precisao nao testavam de verdade):
1. DEPTH (o bloqueio do EFB preto): Aurora reverse-Z (clear 0.0) + GC NDC z [0,-1]. Fix:
   z-fix=-2z-w, clear=1.0-clearVal, glDepthMask(TRUE), respeitar depthFunc/depthUpdate/colorUpdate.
2. mediump overflow (clip ~250k > 65504): two-step GPU (modelview->eye->proj) + near-clipping.
3. SIGBUS: alinhar buffers heap a 16.
4. fragment Utgard mediump-only.
Texturas amostram com detalhe (raw-tex debug). FALTA: material color + lighting reais (alguns
frames lavam branco sem luz), tiling/detalhe. PROCESSO device: pkill dusklight antes do scp,
mask emustation, AURORA_GLES2_CAP -> /tmp/d*.ppm.
