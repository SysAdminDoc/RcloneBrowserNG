#include "interface_polish.h"

#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}
} // namespace

int main() {
  const QColor black(Qt::black);
  const QColor white(Qt::white);

  const QColor success =
      UiPolish::BestContrastingColor(black, QList<QColor>()
                                                << QColor("#008000")
                                                << QColor("#00ff66")
                                                << QColor("#004d1a"));
  const QColor warning =
      UiPolish::BestContrastingColor(black, QList<QColor>()
                                                << QColor("#b8860b")
                                                << QColor("#ffd166")
                                                << QColor("#6b4500"));
  const QColor danger =
      UiPolish::BestContrastingColor(black, QList<QColor>()
                                                << QColor("#cc0000")
                                                << QColor("#ff5f5f")
                                                << QColor("#7a0000"));

  require(success.name() == "#00ff66",
          "success status should choose the bright high-contrast tone");
  require(warning.name() == "#ffd166",
          "warning status should choose the bright high-contrast tone");
  require(danger.name() == "#ff5f5f",
          "error status should choose the bright high-contrast tone");

  const QColor lightFallback = UiPolish::BestContrastingColor(
      white, QList<QColor>() << QColor("#f4f4f4"));
  require(lightFallback == QColor(Qt::black),
          "low-contrast light candidates should fall back to black");

  qInfo() << "All interface polish tests passed.";
  return 0;
}
