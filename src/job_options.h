#pragma once
#include <QListWidget>
#include <qexception.h>
#include <quuid.h>

class JobOptions {
public:
  explicit JobOptions(bool isDownload);
  JobOptions();

  ~JobOptions();

  enum Operation { UnknownOp, Copy, Move, Sync, Bisync };
  enum JobType { UnknownJobType, Upload, Download };

  /*
   * The following enums have their int values synchronized with the
   * list indexes on the gui.  Changes needed to be synchronized.
   */
  enum SyncTiming { During, After, Before, UnknownTiming };
  enum CompareOption {
    SizeAndModTime,
    Checksum,
    IgnoreSize,
    SizeOnly,
    ChecksumIgnoreSize
  };

  QString description;

  JobType jobType;
  Operation operation;
  bool dryRun; // not persisted
  bool sync;
  SyncTiming syncTiming;
  bool skipNewer;
  bool skipExisting;
  bool compare;
  CompareOption compareOption;
  bool verbose;
  bool sameFilesystem;
  bool dontUpdateModified;
  QString transfers;
  QString checkers;
  QString bandwidth;
  QString minSize;
  QString minAge;
  QString maxAge;
  int maxDepth;
  QString connectTimeout;
  QString idleTimeout;
  QString retries;
  QString lowLevelRetries;
  bool deleteExcluded;
  // --server-side-across-configs: let the provider move the bytes between
  // two of its own remotes instead of pulling them through this machine.
  bool serverSideAcrossConfigs;
  QString excluded;
  QString extra;
  QString source;
  QString dest;
  bool isFolder;
  QUuid uniqueId;
  bool DriveSharedWithMe;
  QString heartbeatUrl;
  QString nameTransform;
  QString preCommand;
  QString postCommand;
  QString webhookUrl;
  bool hooksTrusted = false;
  bool watchFolder;
  QString backupDir;
  int backupRetainCount = 0;
  QString conflictResolve;
  bool verifyAfterTransfer = false;
  // Windows: read a local source through a Volume Shadow Copy, so files
  // another program holds open still copy.
  bool vssSnapshot = false;
  // Sync only: --max-delete. rclone deletes this many files, then stops
  // the run with exit 7, so a source that comes back empty can't wipe the
  // destination. -1 is off. Bisync reads --max-delete as a percentage, so
  // it never gets this.
  int maxDelete = -1;

  // Copy and Sync only read the source. Move deletes from it and Bisync
  // writes to it, and a snapshot can't be changed.
  bool snapshotApplies() const {
    return vssSnapshot && (operation == Copy || operation == Sync);
  }

  void setJobType(bool isDownload) {
    jobType = (isDownload) ? Download : Upload;
  }

  QString myName() const {
    return "JobOptions"; // this->staticQtMetaObject.myName();
  }
  QStringList getOptions() const;
  // getOptions() for a preview: a dry run with no --max-delete. rclone counts
  // a dry run's deletes against the cap, so a capped preview stops partway
  // and lists only some of what the sync would do.
  QStringList getPreviewOptions() const;
  // The same for arguments already built, such as a history row's: adds
  // --dry-run after the command, keeps it on (KeepDryRunOn), and drops every
  // --max-delete and --max-delete-size unless the command is bisync (whose
  // dry run stops at a cap exactly where the real run would).
  static QStringList DryRunArguments(QStringList args);
  // Makes sure nothing in the arguments turns a dry run back off: every
  // --dry-run=<false> and -n=<false> (also inside a cluster such as
  // -vn=false) becomes the plain flag, and every global.dry_run option in a
  // connection string is renamed so rclone ignores it. getOptions() runs it
  // for a dry run too.
  static QStringList KeepDryRunOn(QStringList args);

  // The delete caps built arguments carry, read the way rclone reads them:
  // the last --max-delete and --max-delete-size win, and -1 means none.
  struct DeleteCaps {
    qint64 count = -1;
    qint64 bytes = -1;
  };
  static DeleteCaps DeleteCapsIn(const QStringList &args);

  // A size as rclone's SizeSuffix reads it, in bytes (a bare number is
  // KiB); -1 for "off" or text rclone refuses.
  static qint64 SizeSuffixBytes(const QString &text);

  bool operator==(const JobOptions &other) const {
    return uniqueId == other.uniqueId;
  }

  /*
   * This allows the de-serialization method to accomodate changes
   * to the class structure, especially (most easily) added members.
   *
   * Increment the value each time a change is made, emit the new field(s)
   * in the operator<< function, and in operator>> add conditional logic
   * based on the version for reading in the new field(s)
   */
  static const qint32 classVersion;
};

class JobOptionsListWidgetItem : public QListWidgetItem {
public:
  JobOptionsListWidgetItem(JobOptions *jo, const QIcon &icon,
                           const QString &text)
      : QListWidgetItem(icon, text), mJobData(jo) {}

  void SetData(JobOptions *jo) { mJobData = jo; }
  JobOptions *GetData() { return mJobData; }

private:
  JobOptions *mJobData;
};

class SerializationException : public QException {
public:
  QString Message;
  explicit SerializationException(QString msg);
};
