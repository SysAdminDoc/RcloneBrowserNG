#include "interface_polish.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

class InterfacePolishTest : public QObject {
  Q_OBJECT
private slots:
  void darkSuccessColor() {
    const QColor result = UiPolish::BestContrastingColor(
        QColor(Qt::black),
        QList<QColor>() << QColor("#008000") << QColor("#00ff66")
                        << QColor("#004d1a"));
    QCOMPARE(result.name(), QString("#00ff66"));
  }

  void darkWarningColor() {
    const QColor result = UiPolish::BestContrastingColor(
        QColor(Qt::black),
        QList<QColor>() << QColor("#b8860b") << QColor("#ffd166")
                        << QColor("#6b4500"));
    QCOMPARE(result.name(), QString("#ffd166"));
  }

  void darkDangerColor() {
    const QColor result = UiPolish::BestContrastingColor(
        QColor(Qt::black),
        QList<QColor>() << QColor("#cc0000") << QColor("#ff5f5f")
                        << QColor("#7a0000"));
    QCOMPARE(result.name(), QString("#ff5f5f"));
  }

  void lightFallbackToBlack() {
    const QColor result = UiPolish::BestContrastingColor(
        QColor(Qt::white), QList<QColor>() << QColor("#f4f4f4"));
    QCOMPARE(result, QColor(Qt::black));
  }

  void highContrastDetectionDoesNotCrash() {
    bool result = UiPolish::IsHighContrastActive();
    Q_UNUSED(result);
  }

  void bestContrastingWithEmptyListReturnsBlack() {
    const QColor result =
        UiPolish::BestContrastingColor(QColor(Qt::white), QList<QColor>());
    QCOMPARE(result, QColor(Qt::black));
  }

  void bestContrastingWithSingleGoodCandidate() {
    const QColor result = UiPolish::BestContrastingColor(
        QColor(Qt::white), QList<QColor>() << QColor("#000000"));
    QCOMPARE(result.name(), QString("#000000"));
  }

  // The field rule's min-height also matched multi-line editors and replaced
  // their own minimum, so the command output view opened one line tall.
  void stylesheetKeepsMultiLineEditorMinimum() {
    UiPolish::ApplyApplicationStyle(false);
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *editor = new QPlainTextEdit(&host);
    editor->setMinimumHeight(200);
    auto *field = new QLineEdit(&host);
    layout->addWidget(editor);
    layout->addWidget(field);
    host.show();
    QCoreApplication::processEvents();
    QCOMPARE(editor->minimumHeight(), 200);
    QVERIFY2(editor->height() >= 200, "output view shrank below its minimum");
    // Single-line fields still get the shared height.
    QVERIFY2(field->minimumHeight() > 0, "line edit lost its minimum height");
    qApp->setStyleSheet(QString());
  }

  // The style's file icon is a white page with a pale outline, and on the
  // icon-only pickers it is the only thing that says what the button does.
  // WCAG 1.4.11 asks 3:1 of it against the button in both themes.
  void fileIconStandsOutOnButtons() {
    const auto luminance = [](const QColor &color) {
      const auto channel = [](double value) {
        return value <= 0.03928 ? value / 12.92
                                : std::pow((value + 0.055) / 1.055, 2.4);
      };
      return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
             0.0722 * channel(color.blueF());
    };
    for (const bool dark : {false, true}) {
      UiPolish::ApplyApplicationStyle(dark);
      const QColor button = qApp->palette().color(QPalette::Button);
      const QImage image =
          UiPolish::FileIcon().pixmap(18, 18).toImage().convertToFormat(
              QImage::Format_ARGB32);
      const double back = luminance(button);
      double best = 1.0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          const QColor ink = image.pixelColor(x, y);
          const double alpha = ink.alphaF();
          const QColor seen = QColor::fromRgbF(
              static_cast<float>(ink.redF() * alpha + button.redF() * (1 - alpha)),
              static_cast<float>(ink.greenF() * alpha + button.greenF() * (1 - alpha)),
              static_cast<float>(ink.blueF() * alpha + button.blueF() * (1 - alpha)));
          const double front = luminance(seen);
          const double ratio = ((std::max)(front, back) + 0.05) /
                               ((std::min)(front, back) + 0.05);
          best = (std::max)(best, ratio);
        }
      }
      QVERIFY2(best >= 3.0,
               qPrintable(QString("file icon reaches %1:1 on %2 in %3")
                              .arg(best, 0, 'f', 2)
                              .arg(button.name(), dark ? "dark" : "light")));
    }
    qApp->setStyleSheet(QString());
  }
};

QTEST_MAIN(InterfacePolishTest)
#include "interface_polish_test.moc"
