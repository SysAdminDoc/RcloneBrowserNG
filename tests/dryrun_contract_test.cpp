#include "job_options.h"
#include "job_options_store.h"

#include <QBuffer>
#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

bool argsContain(const QStringList &args, const QString &flag) {
  return args.contains(flag);
}

JobOptions *makeTask(JobOptions::Operation op, bool dryRun) {
  auto jo = new JobOptions(false);
  jo->operation = op;
  jo->dryRun = dryRun;
  jo->source = "/src";
  jo->dest = "remote:dst";
  jo->isFolder = true;
  return jo;
}
} // namespace

int main() {
  // Contract 1: dryRun defaults to false
  {
    JobOptions jo;
    require(!jo.dryRun, "dryRun must default to false");
  }

  // Contract 2: dryRun=true produces --dry-run for every operation type
  {
    const JobOptions::Operation ops[] = {JobOptions::Copy, JobOptions::Move,
                                         JobOptions::Sync, JobOptions::Bisync};
    const char *names[] = {"Copy", "Move", "Sync", "Bisync"};
    for (int i = 0; i < 4; ++i) {
      auto jo = makeTask(ops[i], true);
      QStringList args = jo->getOptions();
      require(argsContain(args, "--dry-run"),
              QString("dryRun=true must produce --dry-run for %1").arg(names[i]));
      delete jo;
    }
  }

  // Contract 3: dryRun=false never produces --dry-run for any operation type
  {
    const JobOptions::Operation ops[] = {JobOptions::Copy, JobOptions::Move,
                                         JobOptions::Sync, JobOptions::Bisync};
    const char *names[] = {"Copy", "Move", "Sync", "Bisync"};
    for (int i = 0; i < 4; ++i) {
      auto jo = makeTask(ops[i], false);
      QStringList args = jo->getOptions();
      require(!argsContain(args, "--dry-run"),
              QString("dryRun=false must NOT produce --dry-run for %1")
                  .arg(names[i]));
      delete jo;
    }
  }

  // Contract 4: dryRun is not persisted through save/load round-trip
  {
    auto jo = makeTask(JobOptions::Sync, true);
    jo->description = "test-persist";
    jo->uniqueId =
        QUuid::fromString("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}");

    QList<JobOptions *> tasks;
    tasks.append(jo);

    QByteArray bytes;
    QBuffer out(&bytes);
    require(out.open(QIODevice::WriteOnly), "failed to open write buffer");
    QString error;
    require(WriteJobOptionsStore(&out, tasks, &error),
            "failed to write store: " + error);
    out.close();

    QBuffer in(&bytes);
    require(in.open(QIODevice::ReadOnly), "failed to open read buffer");
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStore(&in);
    require(loaded.error.isEmpty(), "failed to read store: " + loaded.error);
    require(loaded.tasks.size() == 1, "round-trip task count wrong");
    require(!loaded.tasks.first()->dryRun,
            "dryRun must NOT survive save/load round-trip");

    QStringList args = loaded.tasks.first()->getOptions();
    require(!argsContain(args, "--dry-run"),
            "loaded task must NOT produce --dry-run");

    ClearJobOptionsList(&loaded.tasks);
    ClearJobOptionsList(&tasks);
  }

  // Contract 5: --dry-run appears exactly once even with many options enabled
  {
    auto jo = makeTask(JobOptions::Sync, true);
    jo->sync = true;
    jo->syncTiming = JobOptions::After;
    jo->skipNewer = true;
    jo->verbose = true;
    jo->compare = true;
    jo->compareOption = JobOptions::Checksum;

    QStringList args = jo->getOptions();
    int count = 0;
    for (const auto &a : args) {
      if (a == "--dry-run")
        ++count;
    }
    require(count == 1, QString("--dry-run must appear exactly once, got %1")
                            .arg(count));
    delete jo;
  }

  // Contract 6: toggling dryRun on then off produces clean args
  {
    auto jo = makeTask(JobOptions::Copy, true);
    QStringList withDry = jo->getOptions();
    require(argsContain(withDry, "--dry-run"), "initial dry-run missing");

    jo->dryRun = false;
    QStringList withoutDry = jo->getOptions();
    require(!argsContain(withoutDry, "--dry-run"),
            "after clearing dryRun, --dry-run must not appear");
    delete jo;
  }

  qInfo() << "All dry-run contract tests passed.";
  return 0;
}
