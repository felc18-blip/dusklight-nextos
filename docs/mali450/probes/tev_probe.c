/* tev_probe.c — prova as 3 paredes do backend GLES2 juntas, no ES2 estrito:
 *   parede 1: vertex ATTRIBUTES (pos/cor/uv) em vez de vertex pulling por storage buffer
 *   parede 2: UNIFORMS (kcolor, modo) em vez de uniform buffer
 *   parede 3: TEV de 2 estagios em GLSL ES 1.00 (float-only, mediump)
 * TEV emulado:
 *   stage0: cprev = texture(tex, uv).rgb * ras.rgb           (modulate)
 *   stage1: cprev = cprev + u_kcolor.rgb                     (add KColor)
 * Verifica a cor central por calculo independente (CPU) vs readback (GPU).
 * Build: cc tev_probe.c -lEGL -lGLESv2 -o tev_probe
 * Rodar estrito: MESA_GLES_VERSION_OVERRIDE=2.0 MESA_GLSL_VERSION_OVERRIDE=100 ./tev_probe
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define W 64
#define H 64

static const char* VS =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_color;\n"   /* ras color (GX channel) */
    "attribute vec2 a_uv;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_uv;\n"
    "void main(){ v_color=a_color; v_uv=a_uv; gl_Position=vec4(a_pos,0.0,1.0); }\n";

/* TEV em GLSL ES 1.00: dois estagios, mediump, sem inteiros */
static const char* FS =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_kcolor;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_uv;\n"
    "void main(){\n"
    "  vec3 ras = v_color.rgb;\n"
    "  vec3 tex = texture2D(u_tex, v_uv).rgb;\n"
    "  vec3 cprev = tex * ras;\n"          /* stage0 modulate */
    "  cprev = cprev + u_kcolor.rgb;\n"    /* stage1 add kcolor */
    "  cprev = clamp(cprev, 0.0, 1.0);\n"  /* TEV clamp */
    "  gl_FragColor = vec4(cprev, 1.0);\n"
    "}\n";

static GLuint compile(GLenum t, const char* s){
  GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,0); glCompileShader(sh);
  GLint ok=0; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
  if(!ok){ char l[1024]; glGetShaderInfoLog(sh,1024,0,l); fprintf(stderr,"shader: %s\n",l); exit(2);} return sh;
}

int main(void){
  EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint mj,mn; eglInitialize(dpy,&mj,&mn); eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                     EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; EGLint n; eglChooseConfig(dpy,ca,&cfg,1,&n);
  const EGLint pb[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
  EGLSurface sf=eglCreatePbufferSurface(dpy,cfg,pb);
  const EGLint xa[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
  EGLContext cx=eglCreateContext(dpy,cfg,EGL_NO_CONTEXT,xa);
  eglMakeCurrent(dpy,sf,sf,cx);
  printf("GL %s | GLSL %s\n", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

  GLuint prog=glCreateProgram();
  glAttachShader(prog,compile(GL_VERTEX_SHADER,VS));
  glAttachShader(prog,compile(GL_FRAGMENT_SHADER,FS));
  glBindAttribLocation(prog,0,"a_pos");
  glBindAttribLocation(prog,1,"a_color");
  glBindAttribLocation(prog,2,"a_uv");
  glLinkProgram(prog);
  GLint ok=0; glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok){fprintf(stderr,"link\n");return 2;}
  glUseProgram(prog);

  /* textura 2x2: usa o texel (0,0) no centro. valor conhecido: cinza 0.5 */
  unsigned char texdata[]={128,128,128,255, 200,50,50,255, 50,200,50,255, 50,50,200,255};
  GLuint tex; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texdata);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glUniform1i(glGetUniformLocation(prog,"u_tex"),0);

  /* ras color = (0.5,0.5,0.5,1), kcolor = (0.1,0.0,0.2,0) */
  float kr=0.1f,kg=0.0f,kb=0.2f;
  glUniform4f(glGetUniformLocation(prog,"u_kcolor"),kr,kg,kb,0.0f);

  /* quad cobrindo a tela; uv centrado no texel (0,0)=cinza */
  const GLfloat v[]={
    /* pos */    /* color rgba */      /* uv -> texel(0,0) */
    -1,-1,  0.5f,0.5f,0.5f,1.0f,  0.25f,0.25f,
     1,-1,  0.5f,0.5f,0.5f,1.0f,  0.25f,0.25f,
    -1, 1,  0.5f,0.5f,0.5f,1.0f,  0.25f,0.25f,
     1, 1,  0.5f,0.5f,0.5f,1.0f,  0.25f,0.25f,
  };
  GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,0,8*sizeof(float),(void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1,4,GL_FLOAT,0,8*sizeof(float),(void*)(2*sizeof(float)));
  glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,0,8*sizeof(float),(void*)(6*sizeof(float)));

  /* FBO RGBA */
  GLuint rb,fb; glGenRenderbuffers(1,&rb); glBindRenderbuffer(GL_RENDERBUFFER,rb);
  glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA4,W,H);
  glGenFramebuffers(1,&fb); glBindFramebuffer(GL_FRAMEBUFFER,fb);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);

  glViewport(0,0,W,H); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLE_STRIP,0,4); glFinish();

  unsigned char px[4]; glReadPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);

  /* esperado (CPU): tex(0.5)*ras(0.5)=0.25 ; +kcolor ; *255 */
  float er=(0.5f*0.5f+kr), eg=(0.5f*0.5f+kg), eb=(0.5f*0.5f+kb);
  int ER=(int)roundf(er*255), EG=(int)roundf(eg*255), EB=(int)roundf(eb*255);
  printf("TEV GPU = %d,%d,%d | CPU esperado ~ %d,%d,%d\n", px[0],px[1],px[2], ER,EG,EB);
  /* RGBA4 = 4 bits/canal -> tolerancia larga (~17) */
  int tol=20;
  int ok2 = abs(px[0]-ER)<tol && abs(px[1]-EG)<tol && abs(px[2]-EB)<tol;
  printf(ok2 ? "RESULTADO: OK — TEV 2-estagios GLSL ES 1.00 bate com o calculo\n"
             : "RESULTADO: FALHOU — TEV divergiu\n");
  return ok2?0:3;
}
