#include "interface_polish.h"

#include <QTest>

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
};

QTEST_MAIN(InterfacePolishTest)
#include "interface_polish_test.moc"
