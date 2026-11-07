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

class QMouseEvent;

class WaterfallWidget : public QOpenGLWidget, protected QOpenGLFunctions {
  Q_OBJECT
 public:
  static constexpr int kTexW = 1024;  // 显示列数
  // 环形行数：512 行 @30fps = 17 s 存量，画面上可见的是其中 0.65（≈11 s）。
  // 行数跟着帧率一起翻倍，否则提帧率会把历史时长砍半（原来 256 行 @15fps
  // 同样是 17 s 存量 / 11 s 可见）
  static constexpr int kTexH = 512;

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

 signals:
  // 用户点击谱区 → 该列的归一化音频偏移（0..1 = 0..24 kHz）。
  // SSB 模式下 MainWindow 换算成 RF 跳频；AM/FM 解调音频与 RF 无简单映射，不跳
  void freqPicked(double frac);

 protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent*) override;

 private:
  bool buildPrograms();

  // R8 单通道纹理：桌面 GL / GLES3 用 GL_R8+GL_RED；GLES2 上下文没有这两个
  // 常量，退回 GL_LUMINANCE（ES2 下取样 .r 仍是亮度值）
  unsigned tex_ = 0;
  unsigned tex_internal_ = 0;  // GL_R8 或 GL_LUMINANCE
  unsigned tex_format_ = 0;    // GL_RED 或 GL_LUMINANCE
  int next_row_ = 0;  // 下一个写入行 = 当前最旧行（也是显示底边）
  bool row_pending_ = false;
  bool line_dirty_ = true;  // 谱线顶点脏标志（GPU 调度：脏才重算+重传）
  std::vector<uint8_t> row_;
  std::vector<float> line_db_;
  std::vector<float> line_verts_;    // 谱线描边（2N）
  std::vector<float> fill_verts_;    // 谱线下方填充（triangle strip：每列 2 顶点 = 4N float）
  std::vector<float> grid_verts_;    // 频率网格竖线（GL_LINES）
  bool gl_ok_ = true;

  QOpenGLShaderProgram wf_prog_;
  QOpenGLShaderProgram line_prog_;
  unsigned quad_vbo_ = 0;
  unsigned grid_vbo_ = 0;   // 静态网格（initializeGL 一次上传，paintGL 只 bind+draw）
  unsigned line_vbo_ = 0;

  float db_floor_ = -115.0f;
  float db_ceil_ = -15.0f;
};
