// SPDX-License-Identifier: AGPL-3.0-or-later
// LoTW 上传实现——机制移植自原版 LoTW_Upload_Dialog.py（本地 HamLog R1.0.0，
// 师傅自有软件）：TQSL 查找表 / 签名参数 / multipart 上传构造逐项对齐。
#include "logbook/lotw_upload_dialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QNetworkReply>
#include <QUrl>
#include <QVBoxLayout>

namespace radio::logbook {
namespace {
// 原版 TQSLFinder.DEFAULT_PATHS + TQSL_NAMES
const char* kTqslPaths[] = {
    "C:/Program Files/TrustedQSL/tqsl.exe",
    "C:/Program Files (x86)/TrustedQSL/tqsl.exe",
    "C:/TrustedQSL/tqsl.exe",
    "D:/Program Files/TrustedQSL/tqsl.exe",
    "D:/Program Files (x86)/TrustedQSL/tqsl.exe",
    "E:/Program Files/TrustedQSL/tqsl.exe",
    "E:/Program Files (x86)/TrustedQSL/tqsl.exe",
};
constexpr int kUploadTimeoutMs = 120000;
}  // namespace

LotwUploadDialog::LotwUploadDialog(HamLogDb& db, QWidget* parent)
    : QDialog(parent), db_(db) {
  setWindowTitle("LoTW 上传（TQSL 签名 + HTTP 上传）");
  setMinimumWidth(520);
  auto* root = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  auto* adi_row = new QHBoxLayout();
  adi_path_ = new QLineEdit(this);
  adi_path_->setPlaceholderText("ADIF 文件（可先在工作台导出）");
  adi_row->addWidget(adi_path_, 1);
  auto* pick = new QPushButton("浏览…", this);
  connect(pick, &QPushButton::clicked, this, &LotwUploadDialog::onPickAdif);
  adi_row->addWidget(pick);
  form->addRow("ADIF:", adi_row);

  auto* tq_row = new QHBoxLayout();
  tqsl_path_ = new QLineEdit(findTqsl(), this);
  tq_row->addWidget(tqsl_path_, 1);
  auto* find = new QPushButton("查找 TQSL", this);
  connect(find, &QPushButton::clicked, this, &LotwUploadDialog::onFindTqsl);
  tq_row->addWidget(find);
  form->addRow("TQSL:", tq_row);
  root->addLayout(form);

  upload_btn_ = new QPushButton("签名并上传（TQSL → LoTW）", this);
  upload_btn_->setStyleSheet("QPushButton { background: #1565c0; color: white; }");
  connect(upload_btn_, &QPushButton::clicked, this, &LotwUploadDialog::onUpload);
  root->addWidget(upload_btn_);

  log_ = new QTextEdit(this);
  log_->setReadOnly(true);
  log_->setMinimumHeight(140);
  root->addWidget(log_, 1);

  auto* close = new QPushButton("关闭", this);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  root->addWidget(close, 0, Qt::AlignRight);
}

QString LotwUploadDialog::findTqsl() const {
  // 原版 TQSLFinder：默认路径表 → PATH
  for (const char* p : kTqslPaths)
    if (QFileInfo::exists(p)) return p;
  const QStringList paths =
      qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts);
  for (const QString& dir : paths) {
    const QString cand = QDir(dir).filePath("tqsl.exe");
    if (QFileInfo::exists(cand)) return cand;
  }
  return {};
}

void LotwUploadDialog::onPickAdif() {
  const QString path = QFileDialog::getOpenFileName(this, "选择 ADIF", {},
                                                    "ADIF (*.adi)");
  if (!path.isEmpty()) adi_path_->setText(path);
}

void LotwUploadDialog::onFindTqsl() {
  const QString found = findTqsl();
  if (found.isEmpty())
    log_->append("未找到 TQSL——请安装 TrustedQSL 或手动填路径");
  else
    tqsl_path_->setText(found);
}

void LotwUploadDialog::onUpload() {
  const QString tqsl = tqsl_path_->text();
  const QString adi = adi_path_->text();
  if (!QFileInfo::exists(tqsl)) {
    log_->append("TQSL 路径无效（先安装 TrustedQSL）");
    return;
  }
  if (!QFileInfo::exists(adi)) {
    log_->append("ADIF 文件不存在");
    return;
  }
  upload_btn_->setEnabled(false);
  log_->append("① TQSL 签名中…");

  // 签名参数对齐原版 _sign_with_tqsl：-x batch / -d 不弹日期 / -a ADD / -o 输出
  tq8_path_ = QDir::temp().filePath(
      QString("radiosuite_%1.tq8").arg(QDateTime::currentMSecsSinceEpoch()));
  QProcess tqsl_proc(this);
  tqsl_proc.start(tqsl, {"-x", "-d", "-a", "ADD", "-o", tq8_path_, adi});
  tqsl_proc.waitForFinished(60000);
  if (!QFileInfo::exists(tq8_path_) ||
      QFileInfo(tq8_path_).size() == 0) {
    log_->append("签名失败。TQSL 输出：\n" +
                 QString::fromUtf8(tqsl_proc.readAllStandardOutput() +
                                   tqsl_proc.readAllStandardError()));
    upload_btn_->setEnabled(true);
    return;
  }
  log_->append(QString("② 签名成功（%1 B）→ HTTP POST 上传…")
                   .arg(QFileInfo(tq8_path_).size()));

  // multipart 上传（原版 _upload_tq8 构造逐项对齐：upfile 字段 + UA）
  QFile tq8(tq8_path_);
  tq8.open(QIODevice::ReadOnly);
  const QByteArray tq8_data = tq8.readAll();
  tq8.close();
  const QByteArray boundary = "----WebKitFormBoundary" +
                              QByteArray::number(
                                  QDateTime::currentMSecsSinceEpoch(), 16);
  QByteArray body;
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"upfile\"; "
          "filename=\"upload.tq8\"\r\n";
  body += "Content-Type: application/octet-stream\r\n\r\n";
  body += tq8_data;
  body += "\r\n--" + boundary + "--\r\n";

  QNetworkRequest req(QUrl("https://lotw.arrl.org/lotw/upload"));
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                "multipart/form-data; boundary=" + boundary);
  req.setRawHeader("User-Agent", "TQSL/2.7.4 (Windows; en)");
  connect(&nam_, &QNetworkAccessManager::finished, this,
          &LotwUploadDialog::onUploadFinished);
  nam_.post(req, body);
  // 超时保护由 nam_ transferTimeout（Qt6 默认关；这里靠用户关窗中止）
}

void LotwUploadDialog::onUploadFinished(QNetworkReply* reply) {
  reply->deleteLater();
  upload_btn_->setEnabled(true);
  QFile::remove(tq8_path_);
  const QByteArray body = reply->readAll();
  // 响应解析（原版 _parse_upload_response 语义）：ACCEPTED/EXISTS 即成功
  if (reply->error() != QNetworkReply::NoError) {
    log_->append("上传网络错误：" + reply->errorString());
    return;
  }
  const QString text = QString::fromUtf8(body);
  if (text.contains("accepted") || text.contains("ACCEPTED") ||
      text.contains("already exists", Qt::CaseInsensitive)) {
    const QString note = text.contains("already exists", Qt::CaseInsensitive)
                             ? QStringLiteral("（记录已存在）")
                             : QString();
    log_->append(QStringLiteral("✓ LoTW 接受上传") + note);
    log_->append(text.left(600));
  } else {
    log_->append("LoTW 响应（未见接受标记）：\n" + text.left(600));
  }
}

}  // namespace radio::logbook
