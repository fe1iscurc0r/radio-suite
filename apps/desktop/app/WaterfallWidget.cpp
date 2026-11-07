// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/WaterfallWidget.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <algorithm>
#include <cstring>

// GLES2 头文件没有 R8/RED 常量（GLES3 与桌面 GL 才有）：按上下文能力选纹理格式
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif

namespace {

const char* kQuadVert = R"GLSL(
attribute vec2 aPos;
varying vec2 vUv;
void main() {
  vUv = vec2(aPos.x * 0.5 + 0.5, aPos.y * 0.5 + 0.5);
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// GLES 的片元着色器没有默认 float 精度，不声明会直接编译失败（桌面 GL 无此要求）
const char* kWaterfallFrag = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D uTex;
uniform float uRowOff;   // 环形纹理：屏幕底边对应的归一化行号
uniform float uX0;       // 显示缩放：屏幕左缘对应的归一化列（0..1）
uniform float uX1;       // 显示缩放：屏幕右缘对应的归一化列
varying vec2 vUv;
void main() {
  float u = mix(uX0, uX1, vUv.x);
  float t = texture2D(uTex, vec2(u, fract(uRowOff + vUv.y))).r;
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
#ifdef GL_ES
precision mediump float;
#endif
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
  line_dirty_ = true;  // 谱线顶点重算与重传推迟到 paintGL（GPU 调度：脏才动）
}

void WaterfallWidget::mousePressEvent(QMouseEvent* e) {
  // 按下只记录起点：是否框选由移动距离决定，否则释放=点击调频
  if (e->button() != Qt::LeftButton) return;
  pressed_ = true;
  dragging_ = false;
  drag_x0_ = drag_x1_ = static_cast<float>(e->position().x());
}

void WaterfallWidget::mouseMoveEvent(QMouseEvent* e) {
  if (!pressed_) return;
  drag_x1_ = static_cast<float>(e->position().x());
  if (!dragging_ && std::abs(drag_x1_ - drag_x0_) > 6.0f) dragging_ = true;
  if (dragging_) update();  // 橡皮筋重绘
}

void WaterfallWidget::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton || !pressed_) return;
  pressed_ = false;
  if (dragging_) {
    dragging_ = false;
    const float w = static_cast<float>(width());
    const float lo = std::min(drag_x0_, drag_x1_) / w;
    const float hi = std::max(drag_x0_, drag_x1_) / w;
    if (hi - lo > 0.005f) setZoom(lo, hi);  // 太小的框当误触，按点击处理
    else update();
  } else {
    const double frac = std::clamp(e->position().x() / width(), 0.0, 1.0);
    emit freqPicked(frac);
  }
}

void WaterfallWidget::mouseDoubleClickEvent(QMouseEvent*) {
  setZoom(0.0f, 1.0f);  // 双击复位
}

void WaterfallWidget::wheelEvent(QWheelEvent* e) {
  // 滚轮以光标为中心缩放显示区间（纯显示层，分辨率不随缩放增加）
  const float frac = static_cast<float>(e->position().x()) / width();
  const float span = zoom_hi_ - zoom_lo_;
  const float factor = e->angleDelta().y() > 0 ? 1.25f : 0.8f;
  const float new_span = std::clamp(span / factor, 1.0f / 64.0f, 1.0f);
  const float center = zoom_lo_ + frac * span;
  setZoom(center - frac * new_span, center + (1.0f - frac) * new_span);
}

void WaterfallWidget::setZoom(float lo, float hi) {
  lo = std::clamp(lo, 0.0f, 1.0f);
  hi = std::clamp(hi, 0.0f, 1.0f);
  if (hi - lo < 1.0f / 64.0f) {  // 最小 16 列跨度，再小没有信息量
    const float c = (lo + hi) * 0.5f;
    lo = c - 1.0f / 128.0f;
    hi = c + 1.0f / 128.0f;
  }
  zoom_lo_ = lo;
  zoom_hi_ = hi;
  line_dirty_ = true;  // 谱线顶点按新区间重映射
  update();
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

  // R8 环形纹理（GLES2 上下文退回 LUMINANCE：那边没有 R8 纹理/GL_RED）
  const QSurfaceFormat fmt = context() ? context()->format() : QSurfaceFormat();
  const bool es2 = fmt.renderableType() == QSurfaceFormat::OpenGLES &&
                   fmt.majorVersion() < 3;
  tex_internal_ = es2 ? GL_LUMINANCE : GL_R8;
  tex_format_ = es2 ? GL_LUMINANCE : GL_RED;

  glGenTextures(1, &tex_);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(tex_internal_), kTexW,
               kTexH, 0, tex_format_, GL_UNSIGNED_BYTE, nullptr);
  // 预填充底色行（噪底亮度）：不填的话刚启动时还没写过的行是全黑的，
  // 要等瀑布把它们从画面上挤出去才正常
  {
    std::vector<uint8_t> pre(static_cast<size_t>(kTexW), 26);
    for (int r = 0; r < kTexH; ++r)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r, kTexW, 1, tex_format_,
                      GL_UNSIGNED_BYTE, pre.data());
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  // 手机上纹理被放大显示（1024 列铺满约 2300 物理像素，一列 2.25 px），
  // NEAREST 会把每条频迹画成 2 px 宽的台阶——就是「段落感」。双线性把台阶
  // 摊成斜坡；纵向跨环尾的那一次插值取的是时间上相邻的两行，不会串画面
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // 瀑布满幅四边形（两个三角形，顶边 y=0.30 与 paintGL 绘制区带一致——
  // 静态几何一次上传；GPU 调度：paintGL 不再每帧重传 STATIC_DRAW）
  const float quad[] = {-1, -1, 1, -1, 1, 0.30f, -1, -1, 1, 0.30f, -1, 0.30f};
  glGenBuffers(1, &quad_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

  // 频率网格：静态，一次上传
  glGenBuffers(1, &grid_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * grid_verts_.size(),
               grid_verts_.data(), GL_STATIC_DRAW);

  glGenBuffers(1, &line_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(float) * 4 * static_cast<size_t>(kTexW), nullptr,
               GL_DYNAMIC_DRAW);

  glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // vsync：Qt6 的 QSurfaceFormat 默认 swapInterval=1 且 QOpenGLWidget 合成
  // 已对齐 vsync；Android GLES 上若出现撕裂再按平台显式设置（这里不硬编码，
  // 免得桌面驱动不支持 -1 自适应刷新时反而锁死帧率）
}

void WaterfallWidget::resizeGL(int, int) { /* viewport 由 QOpenGLWidget 维护 */ }

void WaterfallWidget::paintGL() {
  glClear(GL_COLOR_BUFFER_BIT);
  if (!gl_ok_) return;

  if (row_pending_) {
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, next_row_, kTexW, 1, tex_format_,
                    GL_UNSIGNED_BYTE, row_.data());
    next_row_ = (next_row_ + 1) % kTexH;
    row_pending_ = false;
  }

  // ---- 瀑布（下 2/3 区带，独立，不与谱线重叠）----
  // 顶边 y=0.30：最新行在瀑布顶部，向下滚动。
  // quad 静态 VBO（initializeGL 一次上传，顶点已含 0.30 边界）
  wf_prog_.bind();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_);
  wf_prog_.setUniformValue("uTex", 0);
  // 瀑布只占纹理高度的 0.65（y=0.30..1.0）：偏移要减去这段，
  // 否则最新一行落在可见区之外，画面固定滞后 ~0.35 屏（约 6 秒 @15fps）。
  wf_prog_.setUniformValue(
      "uRowOff",
      static_cast<float>(next_row_ - 1) / static_cast<float>(kTexH) - 0.65f);
  wf_prog_.setUniformValue("uX0", zoom_lo_);
  wf_prog_.setUniformValue("uX1", zoom_hi_);
  glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
  const int a_pos_wf = wf_prog_.attributeLocation("aPos");
  wf_prog_.enableAttributeArray(a_pos_wf);
  wf_prog_.setAttributeBuffer(a_pos_wf, GL_FLOAT, 0, 2);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  wf_prog_.disableAttributeArray(a_pos_wf);
  wf_prog_.release();

  // ---- 频率网格（静态 VBO：bind+draw，零每帧上传；屏幕等分，与缩放无关）----
  line_prog_.bind();
  line_prog_.setUniformValue("uColor", QColor(255, 255, 255, 26));
  glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
  const int a_pos_grid = line_prog_.attributeLocation("aPos");
  line_prog_.enableAttributeArray(a_pos_grid);
  line_prog_.setAttributeBuffer(a_pos_grid, GL_FLOAT, 0, 2);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(grid_verts_.size() / 2));
  line_prog_.disableAttributeArray(a_pos_grid);
  line_prog_.release();

  // ---- 谱线（上 1/3 区带：基线在 y=0.34，峰值向上到 y=0.96）----
  // GPU 调度：顶点只在谱线数据变脏时重算+重传（line_dirty_）；
  // 1024 次三角函数/帧的 CPU 开销与 24KB/帧的重传全部消掉
  const float span = db_ceil_ - db_floor_;
  if (line_dirty_) {
    const float y0 = 0.34f, y1 = 0.96f;
    // 缩放显示：只重映射 [zoom_lo_, zoom_hi_] 区间内的列到全屏宽
    const int c0 = static_cast<int>(zoom_lo_ * (kTexW - 1));
    const int c1 = static_cast<int>(zoom_hi_ * (kTexW - 1));
    line_count_ = c1 - c0 + 1;
    for (int i = c0; i <= c1; ++i) {
      float norm = (line_db_[static_cast<size_t>(i)] - db_floor_) / span;
      if (norm < 0.0f) norm = 0.0f;
      if (norm > 1.0f) norm = 1.0f;
      const float x = -1.0f + 2.0f *
          ((static_cast<float>(i) / (kTexW - 1)) - zoom_lo_) /
          (zoom_hi_ - zoom_lo_);
      const float y = y0 + (y1 - y0) * norm;
      const size_t j = static_cast<size_t>(i - c0);
      line_verts_[j * 2] = x;
      line_verts_[j * 2 + 1] = y;
      fill_verts_[j * 4] = x;
      fill_verts_[j * 4 + 1] = y0;
      fill_verts_[j * 4 + 2] = x;
      fill_verts_[j * 4 + 3] = y;
    }
    line_dirty_ = false;
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
  glDrawArrays(GL_TRIANGLE_STRIP, 0, line_count_ * 2);
  line_prog_.disableAttributeArray(a_pos_fill);

  // 描边（亮绿，2px 在多数驱动上生效）
  line_prog_.setUniformValue("uColor", QColor(140, 255, 170, 235));
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * line_verts_.size(),
               line_verts_.data(), GL_DYNAMIC_DRAW);
  const int a_pos_line = line_prog_.attributeLocation("aPos");
  line_prog_.enableAttributeArray(a_pos_line);
  line_prog_.setAttributeBuffer(a_pos_line, GL_FLOAT, 0, 2);
  glLineWidth(2.0f);
  glDrawArrays(GL_LINE_STRIP, 0, line_count_);
  line_prog_.disableAttributeArray(a_pos_line);
  line_prog_.release();

  // ---- 文字层（QPainter 叠加在 GL 之上）：音频偏移刻度 ----
  // 本仓瀑布是解调音频的单边谱（2048@48k → 0..24 kHz），不是 RF 频谱：
  // 横轴如实标音频偏移；SSB 模式点击谱区（freqPicked）按模式换算 RF 跳频
  QPainter p(this);
  QFont f = p.font();
  f.setPointSize(8);
  p.setFont(f);
  p.setPen(QColor(174, 182, 192, 190));
  const int w = width();
  const float zspan = zoom_hi_ - zoom_lo_;
  for (int k = 0; k <= 4; ++k) {
    const int x = static_cast<int>(k / 4.0 * (w - 1));
    const float khz = (zoom_lo_ + zspan * k / 4.0f) * 24.0f;
    const QString label = khz < 9.95f ? QString::number(khz, 'f', 1) + "k"
                                      : QString::number(static_cast<int>(khz + 0.5f)) + "k";
    p.drawText(std::max(0, x - 14), height() - 6, 44, 14,
               Qt::AlignLeft | Qt::AlignVCenter, label);
  }
  p.drawText(w - 96, 6, 90, 14, Qt::AlignRight | Qt::AlignVCenter, "音频偏移 kHz");
  // 框选橡皮筋：两根竖线 + 中间淡琥珀填充
  if (pressed_ && dragging_) {
    const int x0 = static_cast<int>(drag_x0_), x1 = static_cast<int>(drag_x1_);
    p.setPen(QColor(255, 179, 0, 220));
    p.drawLine(x0, 0, x0, height());
    p.drawLine(x1, 0, x1, height());
    p.fillRect(std::min(x0, x1), 0, std::abs(x1 - x0), height(),
               QColor(255, 179, 0, 30));
  }
}
