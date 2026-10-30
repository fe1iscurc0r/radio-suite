// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/WaterfallWidget.h — QOpenGLWidget 瀑布 + 频谱线
//
// M0：纹理滚屏（R8 行环 + GLSL colormap）+ GL_LINE_STRIP 谱线。
// M2：滚轮缩放/拖拽框选/双击复位、dsp 线程供给（ring buffer 双缓冲）。
#pragma once

#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <cstdint>
#include <vector>

class WaterfallWidget : public QOpenGLWidget, protected QOpenGLFunctions {
  Q_OBJECT
 public:
  static constexpr int kTexW = 1024;  // 显示列数
  static constexpr int kTexH = 256;   // 瀑布保留行数（环形）

  explicit WaterfallWidget(QWidget* parent = nullptr);
  ~WaterfallWidget() override;

  // 显示映射（dB → 纹理亮度 / 谱线高度）
  float dbFloor() const { return db_floor_; }
  float dbCeil() const { return db_ceil_; }
  void setDbRange(float floor_db, float ceil_db);

 public slots:
  // 传入一列已按显示列数降采样的数据；内部只存副本，GL 上传在 paintGL 统一做
  void pushRow(const uint8_t* row, int cols);
  void pushSpectrum(const float* col_db, int cols);

 protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

 private:
  bool buildPrograms();

  // R8 单通道纹理：桌面 GL / GLES3 用 GL_R8+GL_RED；GLES2 上下文没有这两个
  // 常量，退回 GL_LUMINANCE（ES2 下取样 .r 仍是亮度值）
  unsigned tex_ = 0;
  unsigned tex_internal_ = 0;  // GL_R8 或 GL_LUMINANCE
  unsigned tex_format_ = 0;    // GL_RED 或 GL_LUMINANCE
  int next_row_ = 0;  // 下一个写入行 = 当前最旧行（也是显示底边）
  bool row_pending_ = false;
  std::vector<uint8_t> row_;
  std::vector<float> line_db_;
  std::vector<float> line_verts_;    // 谱线描边（2N）
  std::vector<float> fill_verts_;    // 谱线下方填充（triangle strip：每列 2 顶点 = 4N float）
  std::vector<float> grid_verts_;    // 频率网格竖线（GL_LINES）
  bool gl_ok_ = true;

  QOpenGLShaderProgram wf_prog_;
  QOpenGLShaderProgram line_prog_;
  unsigned quad_vbo_ = 0;
  unsigned line_vbo_ = 0;

  float db_floor_ = -115.0f;
  float db_ceil_ = -15.0f;
};
