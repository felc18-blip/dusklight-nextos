/* gles2_probe.c — valida contexto GLES 2.0 + GLSL ES 1.00 (fidelidade Mali-450) no PC.
 * Headless: pbuffer EGL, render de 1 triangulo, glReadPixels, confere cor central.
 * Disciplina Utgard: ES2 API pura, #version 100, precision mediump, sem vertex texture fetch.
 * Build: cc gles2_probe.c -lEGL -lGLESv2 -o gles2_probe
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 64

static const char* VS =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "attribute vec3 a_col;\n"
    "varying vec3 v_col;\n"
    "void main() {\n"
    "  v_col = a_col;\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char* FS =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec3 v_col;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(v_col, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "FALHA shader: %s\n", log);
    exit(2);
  }
  return s;
}

int main(void) {
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "sem EGLDisplay\n"); return 1; }
  EGLint major, minor;
  if (!eglInitialize(dpy, &major, &minor)) { fprintf(stderr, "eglInitialize falhou\n"); return 1; }
  printf("EGL %d.%d — %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));

  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint cfgAttr[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };
  EGLConfig cfg; EGLint n = 0;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) {
    fprintf(stderr, "sem config ES2\n"); return 1;
  }
  const EGLint pbAttr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "sem contexto ES2\n"); return 1; }
  eglMakeCurrent(dpy, surf, surf, ctx);

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
  printf("GLSL        : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
  printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));

  GLuint prog = glCreateProgram();
  glAttachShader(prog, compile(GL_VERTEX_SHADER, VS));
  glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FS));
  glBindAttribLocation(prog, 0, "a_pos");
  glBindAttribLocation(prog, 1, "a_col");
  glLinkProgram(prog);
  GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) { fprintf(stderr, "link falhou\n"); return 2; }
  glUseProgram(prog);

  /* triangulo que cobre o centro, vermelho */
  const GLfloat verts[] = {
    /* pos */     /* col (R) */
    -0.9f, -0.9f,  1.0f, 0.0f, 0.0f,
     0.9f, -0.9f,  1.0f, 0.0f, 0.0f,
     0.0f,  0.9f,  1.0f, 0.0f, 0.0f,
  };
  GLuint vbo; glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

  /* FBO explicito com renderbuffer RGBA8 (= EFB->textura do renderer real) */
  GLuint fbo, rbo;
  glGenRenderbuffers(1, &rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, rbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "FBO incompleto\n"); return 4;
  }

  glViewport(0, 0, W, H);
  glClearColor(0.0f, 0.0f, 0.3f, 1.0f); /* fundo azul-escuro */
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glFinish();

  unsigned char px[W * H * 4];
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  int c = (H / 2 * W + W / 2) * 4;
  printf("pixel central RGBA = %d,%d,%d,%d\n", px[c], px[c+1], px[c+2], px[c+3]);

  /* salva PPM pra inspecao visual opcional */
  FILE* f = fopen("/tmp/gles2_probe.ppm", "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = H - 1; y >= 0; y--)
      for (int x = 0; x < W; x++) {
        int i = (y * W + x) * 4;
        fputc(px[i], f); fputc(px[i+1], f); fputc(px[i+2], f);
      }
    fclose(f);
  }

  int red_ok = px[c] > 200 && px[c+1] < 60 && px[c+2] < 60;
  printf(red_ok ? "RESULTADO: OK — triangulo vermelho GLSL-ES-1.00 renderizou\n"
                : "RESULTADO: FALHOU — centro nao esta vermelho\n");
  return red_ok ? 0 : 3;
}
