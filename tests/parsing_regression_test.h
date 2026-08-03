#pragma once

#include <QObject>

class ParsingRegressionTest : public QObject {
  Q_OBJECT

private slots:
  void extractsStandardListing();
  void extractsChunkedListing();
  void extractsEmptyListing();
  void extractsNestedMetadata();
  void parsesTransferStats();
  void parsesErrorStats();
  void ignoresNonStatsLine();
  void preservesLargeSizes();
  void preservesUnicodeNames();
  void preservesSpecialPaths();
};
