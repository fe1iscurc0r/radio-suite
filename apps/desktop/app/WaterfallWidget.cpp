// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/WaterfallWidget.h"

#include <QColor>
#include <cstring>

namespace {

const char* kQuadVert = R"GLSL(
attribute vec2 aPos;
varying vec2 vUv;
void main() {
  vUv = vec2(aPos.x * 0.5 + 0.5, aPos.y * 0.5 + 0.5);
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* kWaterfallFrag = R"GLSL(
uniform sampler2D uTex;
uniform float uRowOff;   // 环形纹理：屏幕底边对应的归一化行号
varying vec2 vUv;
void main() {
  float t = texture2D(uTex, vec2(vUv.x, fract(uRowOff + vUv.y))).r;
  t = pow(t, 0.75);      // 伽马提亮暗部
  vec3 c;
  c.r = clamp(1.5 - abs(4.0 * t - 3.0), 0.0, 1.0);
  c.g = clamp(1.5 - abs(4.0 * t - 2.0), 0.0, 1.0);
  c.b = clamp(1.5 - abs(4.0 * t - 1.0), 0.0, 1.0);
  gl_FragColor = vec4(c * 0.95, 1.0);
}
)GLSL";

const char* kLineVert = R"GLSL(
attribute vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

const char* kLineFrag = R"GLSL(
uniform vec4 uColor;
void main() { gl_FragColor = uColor; }
)GLSL";

}  // namespace

WaterfallWidget::WaterfallWidget(QWidget* parent) : QOpenGLWidget(parent) {
  row_.assign(kTexW, 0);
  line_db_.assign(kTexW, db_floor_);
  line_verts_.assign(static_cast<size_t>(kTexW) * 2, 0.0f);
  fill_verts_.assign(static_cast<size_t>(kTexW) * 4, -1.0f);
  grid_verts_.reserve(24);
  for (int k = 0; k <= 4; ++k) {
    const float x = -1.0f + 0.5f * k;
    grid_verts_.push_back(x);
    grid_verts_.push_back(-1.0f);
    grid_verts_.push_back(x);
    grid_verts_.push_back(1.0f);
  }
}

WaterfallWidget::~WaterfallWidget() {
  // 若 GL 资源仍在，在析构里释放需要先 makeCurrent
  makeCurrent();
  if (tex_) glDeleteTextures(1, &tex_);
  if (quad_vbo_) glDeleteBuffers(1, &quad_vbo_);
  if (line_vbo_) glDeleteBuffers(1, &line_vbo_);
  doneCurrent();
}

void WaterfallWidget::setDbRange(float floor_db, float ceil_db) {
  db_floor_ = floor_db;
  db_ceil_ = ceil_db;
  update();
}

void WaterfallWidget::pushRow(const uint8_t* row, int cols) {
  if (cols != kTexW) return;  // 显示列数在 M0 固定；M2 缩放时重映射
  std::memcpy(row_.data(), row, static_cast<size_t>(cols));
  row_pending_ = true;
  update();
}

void WaterfallWidget::pushSpectrum(const float* col_db, int cols) {
  if (cols != kTexW) return;
  std::memcpy(line_db_.data(), col_db, sizeof(float) * static_cast<size_t>(cols));
}

bool WaterfallWidget::buildPrograms() {
  auto ok = [](QOpenGLShaderProgram& p, const char* v, const char* f) {
    return p.addShaderFromSourceCode(QOpenGLShader::Vertex, v) &&
           p.addShaderFromSourceCode(QOpenGLShader::Fragment, f) && p.link();
  };
  if (!ok(wf_prog_, kQuadVert, kWaterfallFrag)) return false;
  if (!ok(line_prog_, kLineVert, kLineFrag)) return false;
  return true;
}

void WaterfallWidget::initializeGL() {
  initializeOpenGLFunctions();
  gl_ok_ = buildPrograms();
  if (!gl_ok_) {
    qWarning("waterfall: shader 编译失败，降级为纯色");
    return;
  }

  // R8 环形纹理
  glGenTextures(1, &tex_);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTexW, kTexH, 0, GL_RED,
               GL_UNSIGNED_BYTE, nullptr);
  // 预填充底色行（噪底亮度），避免启动后前 ~17s（256 行 @15fps）下半屏全黑
  {
    std::vector<uint8_t> pre(static_cast<size_t>(kTexW), 26);
    for (int r = 0; r < kTexH; ++r)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r, kTexW, 1, GL_RED, GL_UNSIGNED_BYTE,
                      pre.data());
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // 瀑布满幅四边形（两个三角形）
  const float quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
  glGenBuffers(1, &quad_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

  glGenBuffers(1, &line_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(float) * 2 * static_cast<size_t>(kTexW), nullptr,
               GL_DYNAMIC_DRAW);

  glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void WaterfallWidget::resizeGL(int, int) { /* viewport 由 QOpenGLWidget 维护 */ }

void WaterfallWidget::paintGL() {
  glClear(GL_COLOR_BUFFER_BIT);
  if (!gl_ok_) return;

  if (row_pending_) {
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, next_row_, kTexW, 1, GL_RED,
                    GL_UNSIGNED_BYTE, row_.data());
    next_row_ = (next_row_ + 1) % kTexH;
    row_pending_ = false;
  }

  // ---- 瀑布（下 2/3 区带，独立，不与谱线重叠）----
  // 顶边 y=0.30：最新行在瀑布顶部，向下滚动
  wf_prog_.bind();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_);
  wf_prog_.setUniformValue("uTex", 0);
  // 瀑布只占纹理高度的 0.65（y=0.30..1.0）：偏移要减去这段，
  // 否则最新一行落在可见区之外，画面固定滞后 ~0.35 屏（约 6 秒 @15fps）。
  wf_prog_.setUniformValue(
      "uRowOff",
      static_cast<float>(next_row_ - 1) / static_cast<float>(kTexH) - 0.65f);
  const float quad[] = {-1, -1, 1, -1, 1, 0.30f, -1, -1, 1, 0.30f, -1, 0.30f};
  glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  const int a_pos_wf = wf_prog_.attributeLocation("aPos");
  wf_prog_.enableAttributeArray(a_pos_wf);
  wf_prog_.setAttributeBuffer(a_pos_wf, GL_FLOAT, 0, 2);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  wf_prog_.disableAttributeArray(a_pos_wf);
  wf_prog_.release();

  // ---- 频率网格（竖线跨全高，暗色）----
  line_prog_.bind();
  line_prog_.setUniformValue("uColor", QColor(255, 255, 255, 26));
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * grid_verts_.size(),
               grid_verts_.data(), GL_STATIC_DRAW);
  const int a_pos_grid = line_prog_.attributeLocation("aPos");
  line_prog_.enableAttributeArray(a_pos_grid);
  line_prog_.setAttributeBuffer(a_pos_grid, GL_FLOAT, 0, 2);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(grid_verts_.size() / 2));
  line_prog_.disableAttributeArray(a_pos_grid);
  line_prog_.release();

  // ---- 谱线（上 1/3 区带：基线在 y=0.34，峰值向上到 y=0.96）----
  const float span = db_ceil_ - db_floor_;
  const float y0 = 0.34f, y1 = 0.96f;
  for (int i = 0; i < kTexW; ++i) {
    float norm = (line_db_[static_cast<size_t>(i)] - db_floor_) / span;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    const float x =
        -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(kTexW - 1);
    const float y = y0 + (y1 - y0) * norm;
    line_verts_[static_cast<size_t>(i) * 2] = x;
    line_verts_[static_cast<size_t>(i) * 2 + 1] = y;
    // 填充：每列两个顶点（底、顶），triangle strip 交错
    fill_verts_[static_cast<size_t>(i) * 4] = x;
    fill_verts_[static_cast<size_t>(i) * 4 + 1] = y0;
    fill_verts_[static_cast<size_t>(i) * 4 + 2] = x;
    fill_verts_[static_cast<size_t>(i) * 4 + 3] = y;
  }

  line_prog_.bind();
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);

  // 填充（半透明绿）
  line_prog_.setUniformValue("uColor", QColor(0, 255, 120, 60));
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * fill_verts_.size(),
               fill_verts_.data(), GL_DYNAMIC_DRAW);
  const int a_pos_fill = line_prog_.attributeLocation("aPos");
  line_prog_.enableAttributeArray(a_pos_fill);
  line_prog_.setAttributeBuffer(a_pos_fill, GL_FLOAT, 0, 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, kTexW * 2);
  line_prog_.disableAttributeArray(a_pos_fill);

  // 描边（亮绿，2px 在多数驱动上生效）
  line_prog_.setUniformValue("uColor", QColor(140, 255, 170, 235));
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * line_verts_.size(),
               line_verts_.data(), GL_DYNAMIC_DRAW);
  const int a_pos_line = line_prog_.attributeLocation("aPos");
  line_prog_.enableAttributeArray(a_pos_line);
  line_prog_.setAttributeBuffer(a_pos_line, GL_FLOAT, 0, 2);
  glLineWidth(2.0f);
  glDrawArrays(GL_LINE_STRIP, 0, kTexW);
  line_prog_.disableAttributeArray(a_pos_line);
  line_prog_.release();
}
