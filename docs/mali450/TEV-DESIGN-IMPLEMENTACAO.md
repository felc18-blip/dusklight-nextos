# Design de implementação do TEV em GLSL ES 1.00 (o maior bloco restante)

Objetivo: build_shader_gles2 config-driven que gera o FS com os estágios TEV reais
(em vez do atual `texture * v_color` fixo). Referência: shader.cpp (gerador WGSL).

## Estrutura (estudada)
- `config.tevStageCount` estágios; cada `TevStage` (gx.hpp:99):
  - colorPass: 4 args (a,b,c,d) GXTevColorArg; alphaPass idem.
  - colorOp/alphaOp (TevOp): op, bias, scale, clamp, outReg (PREV/REG0/1/2).
  - kcSel/kaSel (konst), texMapId/texCoordId, channelId (raster), swap tables.
- Args de cor (shader.cpp:105 color_arg_reg): CPREV/APREV, C0-2/A0-2 (tevreg0-2),
  TEXC/TEXA (sampled{stage}), RASC/RASA (raster color do channel), KONST, ONE, HALF, ZERO.
- Combine (GX_TEV_ADD): `out = (d + mix(a,b,c)) * scale + bias`, clamp 0..1, -> outReg.
  (op SUB = d - mix; outras ops = comparadores, menos comuns.)

## Dados a plumbar (snapshot no DrawData OU uniforms)
1. **Texturas**: cada estágio usa texMapId -> textura. Hoje so tex0. Precisa tex0..N
   (sampledTextures bitset). Bindar varias unidades (já tenho o bind group das texturas;
   estender bindgroup pra capturar tex0..N, nao so binding 0).
2. **Raster color** (RASC/RASA): cor do channel = material color * lighting. 1o cut: cor de
   vertice (ja tenho) OU matColor uniform. Lighting depois.
3. **Konstants** (KONST): g_gxState.kcolors[4] + kcSel (seleciona componente). Uniform vec4[4].
4. **TevRegs** (C0-2): g_gxState.colorRegs (info.loadsTevReg) -> uniform vec4[4].
5. **texcoords**: cada estagio tem texCoordId -> a UV gerada (TexGen) pro tcg correspondente.
   Hoje so uv0. Precisa uv0..N (um por tcg ativo).

## Plano de implementação (incremental, cada passo testavel por captura)
A. **Gerador config-driven base**: build_shader_gles2 le config; declara samplers/uniforms
   usados (tex, kcolor[4], tevreg[4]); gera o FS com a cadeia de estagios (prev acumulando).
   Mapear color_arg_reg -> GLSL (sem swap tables no 1o cut: usar .rgb/.a direto).
B. **Multi-textura**: capturar tex0..7 no bind group; bindar nas unidades; uv0..7 no flat.
C. **Raster real**: matColor uniform + cor de vertice; depois lighting per-vertex.
D. **Konstants + tevregs**: uniforms preenchidos do g_gxState (snapshot no DrawData ou via
   o uniform buffer que build_uniform ja monta — posso LER o uniform buffer no render()).
E. **Swap tables, alpha, ops menos comuns** conforme aparecem.

## Atalho possivel
build_uniform JA monta um uniform buffer com matrizes/kcolors/tevregs/luzes (shader_info.cpp:358).
Em vez de re-snapshotar tudo no DrawData, posso LER esse buffer (g_uniforms em data.uniformRange)
e mandar os pedaços relevantes (kcolors, matColor) como glUniform. Isso reusa o trabalho do GX.

## Estado atual (baseline)
FS = `texture2D(u_tex0, uv) * v_color`. Roda 30fps, intro reconhecivel mas chapada.
O TEV completo e o que da o detalhe/cor/sombra corretos.

---
## PROGRESSO 2026-06-02 (sessao TEV)
- **Passo A**: gerador config-driven feito (le config.tevStages, gera cadeia de estagios
  combine `(d OP mix(a,b,c))*scale+bias` clamp -> reg; prev acumula). REGRESSAO: tela branca.
- **Causa da regressao**: TEV referencia creg (C0-2) e konst, que eu inicializava 0/1.
- **Passo C/D**: alimento valores reais — u_creg[4]=colorRegs, u_kcolor[4]=kcolors;
  konst por-estagio via kcSel/kaSel (kcolor.rgb / componente / fracoes 8/8..1/8).
- **Bug do estado diferido**: render() roda no FIM do frame; ler g_gxState.colorRegs/kcolors
  ali pega o ULTIMO draw (stale). FIX: snapshot por-draw no command_processor (DrawData.
  glColorRegs/glKColors), igual ao glMvp. render() le do DrawData.
- **Verificacao**: dump dos FS gerados (AURORA_GLES2_DUMP_SHADERS -> /tmp/tev_shaders.txt)
  confirma combines corretos: `mix(0,sampled0,tevreg0)`=tex*reg, `mix(sampled0,0,0)`=tex pura,
  multi-estagio acumulando prev. Logica do gerador OK.
- **FALTA (proximos)**: raster real (matColor+lighting; hoje=cor de vertice), multi-textura
  (tex1..7 + uv1..7), swap tables, alpha compare real (u_alphaRef do AlphaCompare), fog.
