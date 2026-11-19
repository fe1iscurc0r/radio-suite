// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/satellite_panel.cpp — 卫星页（桌面/手机共用组件）
#include "satellite_panel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>
#include <ctime>

#include "ui_common/app_settings.h"

namespace {

QString tleDir() {
#ifdef Q_OS_ANDROID
  return QDir(radio::ui::settingsDir()).filePath("tle");
#else
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath("tle");
#endif
}

}  // namespace

namespace radio::app {

SatellitePanel::SatellitePanel(QWidget* parent) : QWidget(parent) {
  // 站坐标/转发频率从设置读（station/lat·lng·alt_km、satellite/dl_hz·ul_hz），
  // 每次计算时现读，设置改动即时生效。
  store_ = std::make_unique<radio::satellite::TleStore>(
      tleDir().toStdString());

  // ---- 布局：纵向预算 ≈204dp，不滚动 ----
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(8, 4, 8, 4);
  lay->setSpacing(4);

  ephemeris_label_ = new QLabel(this);
  ephemeris_label_->setStyleSheet("color: #9e9e9e;");
  lay->addWidget(ephemeris_label_);

  auto* sel_row = new QHBoxLayout();
  sat_combo_ = new QComboBox(this);
  sat_combo_->setMinimumHeight(44);
  sat_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sel_row->addWidget(sat_combo_, 1);
  auto* reload_btn = new QPushButton("重载星历", this);
  reload_btn->setMinimumHeight(44);
  connect(reload_btn, &QPushButton::clicked, this,
          [this] { reloadCatalog(); refreshPasses(); });
  sel_row->addWidget(reload_btn);
  lay->addLayout(sel_row);

  // 选中卫星 → 重建 Tle 对象并刷新过境表
  connect(sat_combo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
    selected_ = idx;
    tle_valid_ = false;
    if (idx >= 0 && idx < static_cast<int>(entries_.size())) {
      const auto& e = entries_[idx];
      tle_ = libsgp4::Tle(e.line1, e.line2);
      tle_valid_ = true;
      refreshPasses();
    }
  });

  now_label_ = new QLabel(this);
  now_label_->setStyleSheet("font-size: 15px; font-weight: bold;");
  lay->addWidget(now_label_);

  pass_label_ = new QLabel(this);
  pass_label_->setWordWrap(true);
  pass_label_->setStyleSheet("color: #cfd8dc;");
  lay->addWidget(pass_label_);

  freq_label_ = new QLabel(this);
  freq_label_->setWordWrap(true);
  lay->addWidget(freq_label_);

  auto* fill_row = new QHBoxLayout();
  tx_label_ = new QLabel(this);
  tx_label_->setWordWrap(true);
  fill_row->addWidget(tx_label_, 1);
  fill_btn_ = new QPushButton("填频（只填不发射）", this);
  fill_btn_->setMinimumHeight(44);
  fill_btn_->setEnabled(false);
  connect(fill_btn_, &QPushButton::clicked, this, [this] {
    if (!tle_valid_ || selected_ < 0) return;
    const auto look = radio::satellite::look_at(
        tle_, radio::satellite::tsince_min(tle_, libsgp4::DateTime::Now()), station());
    const double dl = nominalDownlinkHz();
    const double rx = dl + radio::satellite::doppler_hz(dl, look.range_rate_kms);
    const double tx = radio::satellite::linear_uplink_hz(nominalUplinkHz(),
                                                          look.range_rate_kms);
    emit requestFill(rx, tx, QString("%1 现刻多普勒修正").arg(currentName()));
  });
  fill_row->addWidget(fill_btn_);
  lay->addLayout(fill_row);
  lay->addStretch(1);

  // ---- 定时器：1s 现刻 + 60s 过境/星历重载 ----
  now_timer_ = new QTimer(this);
  now_timer_->setInterval(1000);
  connect(now_timer_, &QTimer::timeout, this, [this] { refreshNow(); });
  now_timer_->start();
  pass_timer_ = new QTimer(this);
  pass_timer_->setInterval(60'000);
  connect(pass_timer_, &QTimer::timeout, this, [this] {
    reloadCatalog();
    refreshPasses();
  });
  pass_timer_->start();
  reloadCatalog();
  refreshPasses();
}

void SatellitePanel::reloadCatalog() {
  // 扫缓存目录（每份 group 文件 = 一个源）：无任何缓存 → 空表 + 「无缓存」。
  entries_.clear();
  selected_ = -1;
  tle_valid_ = false;
  ephemeris_stale_ = false;
  int64_t newest_src = 0;
  std::string newest_group;
  std::error_code ec;
  const auto dir = std::filesystem::path(tleDir().toStdString());
  for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
    if (ec || !de.is_regular_file()) continue;
    const auto p = de.path();
    if (p.extension().string() != ".txt") continue;
    const std::string group = p.stem().string();
    std::vector<radio::satellite::TleEntry> one;
    int64_t src = 0;
    radio::satellite::TleStore store(dir);
    std::string err;
    if (!store.loadCatalog(group, &one, &src, &err)) continue;
    entries_.insert(entries_.end(), one.begin(), one.end());
    if (src > newest_src) {
      newest_src = src;
      newest_group = group;
    }
  }
  // 下拉框重填（保留旧选择按名字匹配）
  const QString prev = selected_ >= 0 ? currentName() : QString();
  sat_combo_->clear();
  for (const auto& e : entries_)
    sat_combo_->addItem(QString("%1 (%2)").arg(e.name.c_str(), newest_group.empty()
                                                      ? "?"
                                                      : newest_group.c_str()));
  // ...简化：条目分组标签按最新缓存组标注，够用且不骗人
  const int idx = sat_combo_->findText(prev);
  if (idx >= 0) {
    sat_combo_->setCurrentIndex(idx);
  } else if (!entries_.empty()) {
    sat_combo_->setCurrentIndex(0);  // 首载自动选中第一颗
  }
  sat_combo_->setEnabled(!entries_.empty());
  fill_btn_->setEnabled(false);
  // 星历时效行
  if (newest_src == 0) {
    ephemeris_label_->setText("无星历缓存（设置里联网拉取后可用）");
  } else {
    const int64_t now = std::time(nullptr);
    const double age_min = (now - newest_src) < 0
                               ? -1.0
                               : (now - newest_src) / 60.0;
    const bool stale =
        radio::satellite::TleStore::isStale(now, newest_src, 2 * 60);
    ephemeris_label_->setText(QString(
        "星历 %1 · %2 条 · 组 %3%4")
        .arg(age_min < 0 ? "?" : QString::number(age_min, 'f', 0))
        .arg(entries_.size())
        .arg(newest_group.c_str())
        .arg(stale ? " · 已过期（过境表置灰，不许填频）" : ""));
    if (stale) {
      ephemeris_label_->setStyleSheet("color: #ef5350;");
      ephemeris_stale_ = true;
    }
  }
}

void SatellitePanel::refreshNow() {
  if (!tle_valid_) {
    now_label_->setText("未选中卫星（无缓存或未选）");
    pass_label_->setText("—");
    freq_label_->setText("—");
    tx_label_->setText("—");
    fill_btn_->setEnabled(false);
    return;
  }
  try {
    const auto now = libsgp4::DateTime::Now();
    const double ts = radio::satellite::tsince_min(tle_, now);
    const auto look = radio::satellite::look_at(tle_, ts, station());
    const double dl = nominalDownlinkHz();
    const double ul = nominalUplinkHz();
    const double dop = radio::satellite::doppler_hz(dl, look.range_rate_kms);
    const double rx = dl + dop;
    const double tx = radio::satellite::linear_uplink_hz(ul, look.range_rate_kms);
    if (dl <= 0 || ul <= 0) {
      freq_label_->setText(
          "未配置转发频率（设置键 satellite/dl_hz 与 satellite/ul_hz，Hz）");
      tx_label_->setText("—");
      fill_btn_->setEnabled(false);
    } else {
      now_label_->setText(QString("az %1° el %2° 斜距 %3 km ṙ %4 km/s")
                              .arg(look.az_deg, 0, 'f', 1)
                              .arg(look.el_deg, 0, 'f', 1)
                              .arg(look.range_km, 0, 'f', 0)
                              .arg(look.range_rate_kms, 0, 'f', 2));
      freq_label_->setText(QString("RX %1 kHz（下行 %2 + 多普勒 %3）")
                               .arg(rx / 1e3, 0, 'f', 2)
                               .arg(dl / 1e3, 0, 'f', 2)
                               .arg(dop / 1e3, 0, 'f', 3));
      tx_label_->setText(QString("TX 预补偿 %1 kHz（标称 %2）")
                             .arg(tx / 1e3, 0, 'f', 2)
                             .arg(ul / 1e3, 0, 'f', 2));
      fill_btn_->setEnabled(true);
    }
  } catch (const std::exception& e) {
    now_label_->setText(QString("传播失败: %1").arg(e.what()));
    fill_btn_->setEnabled(false);
  }
}

void SatellitePanel::refreshPasses() {
  if (!tle_valid_ || ephemeris_stale_) {
    if (ephemeris_stale_) {
      pass_label_->setText("星历已过期——过境表置灰，不许填频");
      pass_label_->setEnabled(false);
    } else {
      pass_label_->setText("—");
    }
    fill_btn_->setEnabled(false);
    return;
  }
  pass_label_->setEnabled(true);
  try {
    const auto now = libsgp4::DateTime::Now();
    const double ts0 = radio::satellite::tsince_min(tle_, now);
    const auto passes =
        radio::satellite::find_passes(tle_, station(), ts0, ts0 + 24 * 60, 5.0);
    if (passes.empty()) {
      pass_label_->setText("未来 24h 无仰角 ≥5° 的过境");
      fill_btn_->setEnabled(false);
      return;
    }
    const auto& p = passes.front();
    const auto fmt = [this](double min) {
      const auto dt =
          tle_.Epoch() + libsgp4::TimeSpan(0, 0, static_cast<int>(min), 0);
      return QString("%1:%2:%3")
          .arg(dt.Hour(), 2, 10, QChar('0'))
          .arg(dt.Minute(), 2, 10, QChar('0'))
          .arg(dt.Second(), 2, 10, QChar('0'));
    };
    pass_label_->setText(QString("下次过境 AOS %1 TCA %2 LOS %3 · 最大仰角 %4°")
                             .arg(fmt(p.aos_min), fmt(p.tca_min), fmt(p.los_min),
                                  QString::number(p.max_el_deg, 'f', 1)));
    fill_btn_->setEnabled(true);
  } catch (const std::exception& e) {
    pass_label_->setText(QString("过境计算失败: %1").arg(e.what()));
    fill_btn_->setEnabled(false);
  }
}

radio::satellite::Station SatellitePanel::station() const {
  auto st = radio::ui::appSettings();
  return {st.value("station/lat", 39.9).toDouble(),
          st.value("station/lng", 116.4).toDouble(),
          st.value("station/alt_km", 0.05).toDouble()};
}

double SatellitePanel::nominalDownlinkHz() const {
  auto st = radio::ui::appSettings();
  return st.value("satellite/dl_hz", 0.0).toDouble();
}

double SatellitePanel::nominalUplinkHz() const {
  auto st = radio::ui::appSettings();
  return st.value("satellite/ul_hz", 0.0).toDouble();
}

QString SatellitePanel::currentName() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size()))
    return QString();
  return QString::fromStdString(entries_[selected_].name);
}

}  // namespace radio::app
