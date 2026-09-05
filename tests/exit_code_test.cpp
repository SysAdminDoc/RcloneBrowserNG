#include "rclone_exit_code.h"

#include <QSet>
#include <QTest>

using RcloneExitCode::Describe;
using RcloneExitCode::Outcome;

// Every code below was provoked against rclone v1.75.0 on 2026-09-05 rather
// than copied from the docs table, which the v1.69.0 changelog warned had
// moved. Two entries in that table are wrong: an unknown flag exits 2, not
// 1, and a bisync abort exits 7, not 2.
class ExitCodeTest : public QObject {
  Q_OBJECT

private slots:
  void limitsAreNotFailures_data() {
    QTest::addColumn<int>("code");
    // copy --max-transfer 100k over 4x200k files
    QTest::newRow("8 max-transfer") << 8;
    // copy --error-on-no-transfer with nothing to do
    QTest::newRow("9 no transfer") << 9;
    // copy --max-duration 1ms --cutoff-mode hard
    QTest::newRow("10 max-duration") << 10;
  }

  void limitsAreNotFailures() {
    QFETCH(int, code);
    const auto meaning = Describe(code);
    QCOMPARE(meaning.outcome, Outcome::CompletedWithLimit);
    QVERIFY2(RcloneExitCode::IsSuccessful(code),
             "a job stopped at a limit the user set has done its work");
    QVERIFY(!RcloneExitCode::IsRetryable(code));
  }

  void missingPathsAndTemporaryFaultsOfferRetry_data() {
    QTest::addColumn<int>("code");
    QTest::newRow("3 directory not found") << 3; // lsd of a missing directory
    QTest::newRow("4 file not found") << 4;      // deletefile on a missing file
    QTest::newRow("5 temporary error") << 5;
  }

  void missingPathsAndTemporaryFaultsOfferRetry() {
    QFETCH(int, code);
    QVERIFY(RcloneExitCode::IsRetryable(code));
    QCOMPARE(Describe(code).outcome, Outcome::Retryable);
    QVERIFY(!RcloneExitCode::IsSuccessful(code));
  }

  void realFailuresStayFailures_data() {
    QTest::addColumn<int>("code");
    QTest::newRow("1 operation failed") << 1;  // moveto with a missing source
    QTest::newRow("2 bad command") << 2;       // copy with an unknown flag
    QTest::newRow("6 finished with errors") << 6;
    QTest::newRow("7 fatal") << 7;             // bisync without --resync
  }

  void realFailuresStayFailures() {
    QFETCH(int, code);
    QCOMPARE(Describe(code).outcome, Outcome::Failed);
    QVERIFY(!RcloneExitCode::IsSuccessful(code));
    QVERIFY(!RcloneExitCode::IsRetryable(code));
  }

  void zeroIsSuccess() {
    QCOMPARE(Describe(0).outcome, Outcome::Success);
    QVERIFY(RcloneExitCode::IsSuccessful(0));
    QVERIFY(!RcloneExitCode::IsRetryable(0));
  }

  void everyDocumentedCodeHasItsOwnMessage() {
    QSet<QString> names;
    QSet<QString> explanations;
    for (int code = 0; code <= 10; ++code) {
      const auto meaning = Describe(code);
      QVERIFY2(!meaning.name.isEmpty(), qPrintable(QString::number(code)));
      QVERIFY2(!meaning.explanation.isEmpty(),
               qPrintable(QString::number(code)));
      names.insert(meaning.name);
      explanations.insert(meaning.explanation);
    }
    // "Failed" for everything is what this replaces.
    QCOMPARE(names.size(), 11);
    QCOMPARE(explanations.size(), 11);
  }

  // Cancelling a transfer never produces an rclone exit code. On Windows
  // QProcess::terminate() is a no-op for a console child, so JobWidget::cancel
  // always falls through to kill() and the process reports 62097; on POSIX the
  // kill reports signal 9, which the rclone table calls "Nothing to transfer"
  // and IsSuccessful() accepts. Reading either through the table put
  // "Exit code 62097" in the job history next to a card saying "Cancelled",
  // and would have recorded a killed transfer as a success on Linux.
  void aCancelledJobIsNeverDescribedByItsExitCode() {
    for (int code : {62097, 9, 15, 1, 0}) {
      const auto outcome = RcloneExitCode::DescribeProcess(code, true, true);
      QCOMPARE(outcome.name, QStringLiteral("Cancelled"));
      QVERIFY2(!outcome.success, qPrintable(QString::number(code)));
      QVERIFY2(!outcome.completedFully, qPrintable(QString::number(code)));
    }
  }

  void aCrashedProcessIsNotReadAsAnExitCode() {
    const auto outcome = RcloneExitCode::DescribeProcess(9, true, false);
    QVERIFY2(outcome.name != Describe(9).name, qPrintable(outcome.name));
    QVERIFY(!outcome.success);
    QVERIFY(!outcome.completedFully);
  }

  // The looser flag gates the label; the stricter one gates `rclone purge` of
  // old backup directories and a post-transfer check of the two ends. A run
  // stopped at --max-transfer or --max-duration is a success that did not
  // finish the copy, so it must not reach either.
  void aLimitedRunSucceedsWithoutCountingAsComplete() {
    for (int code : {8, 9, 10}) {
      const auto outcome = RcloneExitCode::DescribeProcess(code, false, false);
      QVERIFY2(outcome.success, qPrintable(QString::number(code)));
      QVERIFY2(!outcome.completedFully, qPrintable(QString::number(code)));
    }

    const auto clean = RcloneExitCode::DescribeProcess(0, false, false);
    QVERIFY(clean.success);
    QVERIFY(clean.completedFully);
  }

  void aPlainFailureIsNeitherSuccessfulNorComplete() {
    for (int code : {1, 2, 3, 4, 5, 6, 7, 42, -1}) {
      const auto outcome = RcloneExitCode::DescribeProcess(code, false, false);
      QVERIFY2(!outcome.success, qPrintable(QString::number(code)));
      QVERIFY2(!outcome.completedFully, qPrintable(QString::number(code)));
    }
  }

  void unknownCodesSaySoRatherThanGuessing() {
    const auto meaning = Describe(42);
    QCOMPARE(meaning.outcome, Outcome::Failed);
    QVERIFY2(meaning.name.contains("42"), qPrintable(meaning.name));
    QVERIFY(!meaning.explanation.isEmpty());
    QVERIFY(!RcloneExitCode::IsSuccessful(42));
  }
};

QTEST_APPLESS_MAIN(ExitCodeTest)
#include "exit_code_test.moc"
