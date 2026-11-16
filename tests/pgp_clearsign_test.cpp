#include "big_uint.h"
#include "pgp_clearsign.h"
#include "pgp_test_signer.h"

#include <QTest>

namespace {

// rclone's own downloads.rclone.org/v1.76.0/SHA256SUMS, byte for byte.
// GnuPG 2.4 reports "Good signature from Nick Craig-Wood" for it.
const char kRealSums1760[] = R"(-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA1

c047a1090c30086da45b7e2a91119947bb3d3e55712171b0e5b009ed4a8855fa  rclone-v1.76.0-aix-ppc64.zip
3bd27243a9a4c116961138b0399c00f81bb86b15dd643b0362608b679c8c731d  rclone-v1.76.0-freebsd-386.zip
7458bfd24c51f705565d29b7e29598e9ef527b41d0944fa3142633dbd84d6ab1  rclone-v1.76.0-freebsd-amd64.zip
e2924e53357c09537416ef26c78dab78fb23748a68b304c2e56dd8563add9bdc  rclone-v1.76.0-freebsd-arm-v6.zip
f610ad08fb8fac1eed3bcedc5805096f5a74f6ec82d883c7d3ba3ee0ef65009a  rclone-v1.76.0-freebsd-arm-v7.zip
11d9bc1ffd6b805bd3f804be9e606ca06865883cb995ba51f3b4e9a00decd90d  rclone-v1.76.0-freebsd-arm.zip
b8a95d0368963edf845bbcb7b5f2696673b0bfd8063dd92e7f9bd8a7d40d468c  rclone-v1.76.0-linux-386.deb
43e558cc9669b45bf00c70b576de749216354c9386808350cf80c153f040df08  rclone-v1.76.0-linux-386.rpm
ac5a696aff30c2806475d7c9ae6bbb4eeda47067e5add32448bf33db54552a09  rclone-v1.76.0-linux-386.zip
9db7ee9b725bce7cdd195b9183449a4c6fb93cad3e6578e70b87c0bc7f6062e9  rclone-v1.76.0-linux-amd64.deb
a4615495895fec32671c12b45b92f59c60a97fccd3c61a9e02dc3dc2e927a74f  rclone-v1.76.0-linux-amd64.rpm
b00f00b3266c3764413afe599d3ec316f6f43b3e0ea8a0bc1d186d2133e46df7  rclone-v1.76.0-linux-amd64.zip
c486d404ecbc4bb3d54052a0efaf9e652cf0d2c645e4523f7a2aefa112fe029c  rclone-v1.76.0-linux-arm64.deb
b961a502b9480583a0f3bc9670718aa65a0dc9ed3fdbb4ef99b102acff62ec5c  rclone-v1.76.0-linux-arm64.rpm
ee169016675e23a69d77240565133ff7dbc9b33d16fa8f7053d4af39f1eaeb78  rclone-v1.76.0-linux-arm64.zip
44fa3d182de9a064218b9c314869baf48c53e98f8fa4733ec83a1f7151db8f1f  rclone-v1.76.0-linux-arm.deb
010fc27a903f1a462879c80e1ff03dd2e912fb11fbef5f4bd1acf2b1043a1f78  rclone-v1.76.0-linux-arm.rpm
9bade92333502b2cda6c33f745d14cf7db4f14924217ae50c7b834a19c6a9d48  rclone-v1.76.0-linux-arm-v6.deb
5608920af2947a8b3fb7b6400247c4ce9908ccd41334a62a4e2c98ddf2807356  rclone-v1.76.0-linux-arm-v6.rpm
bcca617da41b3be618cc6af89680574d73d746a65a896975bc75e0622683c275  rclone-v1.76.0-linux-arm-v6.zip
5406975674ce3436626042a600635a4f6157832fa0d786018f41fca223e33c0f  rclone-v1.76.0-linux-arm-v7.deb
04f5f47a6d8be79383739b705b2860dfb564e712dcfea46c6b061f03782ee989  rclone-v1.76.0-linux-arm-v7.rpm
090e1e2ce0f2a27c6c5ad5b2a2f7bbf7570933f3523e8bf83cb25ee50115f885  rclone-v1.76.0-linux-arm-v7.zip
ecd7145f2565a40d1318c8d79866e6294503bd17abdb17e39e1157eefc8fa72a  rclone-v1.76.0-linux-arm.zip
23a640e6182bbac1a18af5ef4520aa4d637a533251387bc4ea0874827fe7caf4  rclone-v1.76.0-linux-mips.deb
d6c0380f1dcef7e2710827752b15064c7314afb19e54c7a5ff40ca1cc9362072  rclone-v1.76.0-linux-mipsle.deb
a646c21a706e74d93a7171d078fe3bc5a83814b0f58e8905e5bf01c7018b8fb2  rclone-v1.76.0-linux-mipsle.rpm
5c54caf176986d6886037bc684a5d39af2143382af1ddde11f54dcd193bc7702  rclone-v1.76.0-linux-mipsle.zip
5f12db13c619a51a83000ea1d9d78fb33ac3cc1e150eb14b2029b70e20d2f67e  rclone-v1.76.0-linux-mips.rpm
b96634443936b4368189fe3534e3cc69fafed47bb6c33550604d7c206d1276d0  rclone-v1.76.0-linux-mips.zip
487a8996b13df4c1016d8275d7c4a4c6f9e199eb5a63ea734b24b68a5ba1c803  rclone-v1.76.0-netbsd-386.zip
cd1f3def306101874c6cfe08f36db8ba64ddacd76735f77077897347de876b0f  rclone-v1.76.0-netbsd-amd64.zip
4801c8cdb6d0b41aa52d22d5314e1e6a37fb43cceede730a4d70998cfba8b0ef  rclone-v1.76.0-netbsd-arm-v6.zip
5c59f491a894427d5cd0322d33eec7474edf061054abfd7b33995b3bde50cea5  rclone-v1.76.0-netbsd-arm-v7.zip
fd99c49d8524eee7a74da70c4148a0dd579f5605b49bc5ac71d056a000bd172c  rclone-v1.76.0-netbsd-arm.zip
1e76dde677544ecc3b879d667747a09450b0853fb93757dead454b5d7502a64b  rclone-v1.76.0-openbsd-386.zip
2746209d29d33f1d68898e14aee10ed8d52054b3696df40c648afbb65e77e686  rclone-v1.76.0-openbsd-amd64.zip
4f5835733d52b818340f97b1fc0348a70461534d123363a183ca2b660219595a  rclone-v1.76.0-osx-amd64.zip
e72dc90b70f4947a7b72b61bde4a4fbba5777b2874fb448951ffdde67ebd0ab7  rclone-v1.76.0-osx-arm64.zip
7f9e3c94c28dc9000598076a006331a508a1060ee6c938e24c960fedcc1e7f57  rclone-v1.76.0-plan9-386.zip
2be357b933e35c48b6d1c9707fe3a978e62908bf348b0b288797f6e79e5781b5  rclone-v1.76.0-plan9-amd64.zip
27fbd5d8daa690dc43b166ca9d28d9dca3c4c09bc4c8cea56f87b54319e6a559  rclone-v1.76.0-solaris-amd64.zip
fa2b2e4ae56c4f1b91102285b736596e2a028be77cb2aaa5a0df53f3a7fc5902  rclone-v1.76.0.tar.gz
7706e9d6b11971a096406a1e876db3946c73f11a64327a7457545443b13f4cc0  rclone-v1.76.0-vendor.tar.gz
b9c8d062ff61e2addfc31c1981219d90f99e0012cba45b649f728082ad810d5c  rclone-v1.76.0-windows-386.zip
8b4fd2737ade4e70c057ef4a49deb7cffe94b0fb9d7587c189d149823f21a1dc  rclone-v1.76.0-windows-amd64.zip
c406c8a69a5a408e50624aa990fbb0a6266b3a4e83782a94de3cb96f50f86f10  rclone-v1.76.0-windows-arm64.zip
-----BEGIN PGP SIGNATURE-----

iF0EARECAB0WIQT79zfs6firGGBL0qyTk14C/ztU+gUCau35RAAKCRCTk14C/ztU
+nzmAKCmjwQrt6tn9hphj6Rq22kGSrok8ACdHXYVz1s/aVo5U3osVp4s+ANnuKk=
=dvhP
-----END PGP SIGNATURE-----
)";

// downloads.rclone.org/v1.75.1/SHA256SUMS, a second real signature.
const char kRealSums1751[] = R"(-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA1

b33db81f432d32702d386d15e774610e45acb3c0df10af4f45efc113e21c0534  rclone-v1.75.1-aix-ppc64.zip
cd86b83758fd2c8adcdca52af092b0bd71c7c5c4703dc5d82d4f2d7cbaa11ba4  rclone-v1.75.1-freebsd-386.zip
ef2a9e8536126f21704e3e37d01f0969f79fa901a940812e7e8a06dd46aa4ae5  rclone-v1.75.1-freebsd-amd64.zip
1de647b616755c980439877a332645d5540462a5bf537d96f6580941b47f4cfd  rclone-v1.75.1-freebsd-arm-v6.zip
f4f02593984368f3cf10cca0b7993b6e0fd306f33a52e8d450ed8a27a6902404  rclone-v1.75.1-freebsd-arm-v7.zip
185727b6136c7b32fa5b7fef196b953046d52e36854a87d13fefb784f89709ea  rclone-v1.75.1-freebsd-arm.zip
58b387dec303ea7bde8bd557ada8bb3050fdcee9c057c1e97fef835a81a8557e  rclone-v1.75.1-linux-386.deb
bfe352d4d74f3001c75167d99caf4570d6293e352204a8c7d5ffac78738c56c6  rclone-v1.75.1-linux-386.rpm
9250a48ace446b32ad3f3cb26e0744f9245efb65ef4f51dfb416288950b84285  rclone-v1.75.1-linux-386.zip
09c9f7606ed9e31eecc1eec26a89992cf2931a8d2d1a5f0ae2bb1c11630ffb15  rclone-v1.75.1-linux-amd64.deb
812d2ded3cf63dea9aa3ed082f185d975c85e1dd0a12f931f7148fb115ba5509  rclone-v1.75.1-linux-amd64.rpm
982b5aa772841168f8e380f139e9e787b2a105403e32b94da8676a0e1c0a13ab  rclone-v1.75.1-linux-amd64.zip
773f3a76615f91f7d4654183a537afddce3343c8d99ac1d74984f060f2ade2d9  rclone-v1.75.1-linux-arm64.deb
ce1cdfcf8083b9f6c6ff14457db3c7d3d365bb0a5d071cf6e2780310fe640514  rclone-v1.75.1-linux-arm64.rpm
03f2504174034b6d004152ed7369251c9a9ec1f7e0836eda420f5c7a5ec0dff9  rclone-v1.75.1-linux-arm64.zip
8b21c1002d349688c2b519df56b5cabc618ba51a961c71f4ccf228f46dbae56b  rclone-v1.75.1-linux-arm.deb
c0db0ce36094b6fbb207e6c53d331e2d4c7cb9d809bf000c2469de1c6f896d35  rclone-v1.75.1-linux-arm.rpm
3197b710107e8a811a13ffbdbe7ecc69d2c44a03c53e4e88dcb46b6d02de6dba  rclone-v1.75.1-linux-arm-v6.deb
4c20304806453c6b103c87bcf375e63b00105c1ee4e5eca5249a74a80e395108  rclone-v1.75.1-linux-arm-v6.rpm
d81d5c84326e4179ff5ddcb0ecff4c283cdd9a4e23a43a68f1c16d505fb859b8  rclone-v1.75.1-linux-arm-v6.zip
fd55fef473a0645c98f0314e72936b3cc08283a511e3a89d74996a17ce6f51d7  rclone-v1.75.1-linux-arm-v7.deb
90060839bcd70bcff741405881f39896e9a5136ac8cc129b45a9565c8cd21329  rclone-v1.75.1-linux-arm-v7.rpm
33c683053b677d9a89d4a985e8a25cfed7c8a95dd8379e367a5c73d8745356c3  rclone-v1.75.1-linux-arm-v7.zip
6ea78cafa96a55f94da6a644d5057e8dc8ecfff9df0ab4011ce77e1fc654d114  rclone-v1.75.1-linux-arm.zip
9d14359a4cacfabe1e1717c67425c90eb1efee488e428bac6108b2a822a7a8b1  rclone-v1.75.1-linux-mips.deb
cad0e97ee94162f96f26b9e532fd7bd9192b561a6f06ca6f7e39afcc617411fd  rclone-v1.75.1-linux-mipsle.deb
eaf05110ed01f8bd39bac5724cd6a227c22317b752424770511f9ab13e276acc  rclone-v1.75.1-linux-mipsle.rpm
dc1f2a3654ff63fc639af06ff50985dd9f33fb3f4315b3237ce07d95978f4a71  rclone-v1.75.1-linux-mipsle.zip
6fb9aa533049b4478e76d999ee030739ee0b98f4cbf8ef67a437aae4917c1805  rclone-v1.75.1-linux-mips.rpm
bf0609dc90645e3e60598e3e8204f92c8112df643ccb2f06483f3fe390a57b9f  rclone-v1.75.1-linux-mips.zip
1ca162aba6a591b193edfa047863ddfd5143764577773494b376c31f9fd00b7b  rclone-v1.75.1-netbsd-386.zip
df8546109281b831342a65d64a0ea7a3324c42d46014c8b25b7f26abf6bd5d99  rclone-v1.75.1-netbsd-amd64.zip
f17b327b7a53aa15e78c338e8dadd5fc537d8a6e87feaea9f590d3c3281152f4  rclone-v1.75.1-netbsd-arm-v6.zip
c8f837d8d346ef2253c7206825e80dacf500bba6d27ac0a636ba9bb830c8a67c  rclone-v1.75.1-netbsd-arm-v7.zip
5170c7b5c60ad535ef9dd1272100fc89dbe7bdd7be7e2fbfa00385c70f7741b5  rclone-v1.75.1-netbsd-arm.zip
d92f24c1d7d6a81bbd6c5298070c533930d34468a5641a4aa51d3620f9bafc6e  rclone-v1.75.1-openbsd-386.zip
e5b396bacc5f320d950d702979e0d025fa623d3ccdca5f95a9d86428c8d9dfda  rclone-v1.75.1-openbsd-amd64.zip
29253d0288b8fbbac46baad6e5f6add6cb01d462c79f10805bbd4631c4cdf82c  rclone-v1.75.1-osx-amd64.zip
c61d7a371c62bcbbe882c3423aa4b8bf63485c248dd0f692997b8f0c3f6d0c6f  rclone-v1.75.1-osx-arm64.zip
dc54020ee1642b9277e43f78b5790a0e7af6c7d99afa35ef29d0e1da61fae0c0  rclone-v1.75.1-plan9-386.zip
5052136c0eb606d9884e9fb0369fbe48c547e5d9593b2685f702ff8c094f5c27  rclone-v1.75.1-plan9-amd64.zip
188874a7950a40f88bdc6248137bcd77ba349dd927c84eb2507d4467fdd4797c  rclone-v1.75.1-solaris-amd64.zip
6ada25ecda8b3c8d971ed0aae1167e44d257dbcbf0be6598bf6c127daca2e13f  rclone-v1.75.1.tar.gz
a43cd21f71344ff96b3b00217196c069f2f28fa29835cd61cfa94edb1aa5b93b  rclone-v1.75.1-vendor.tar.gz
42257481a961b39cdee3b3eea7b9945a1961825e332d07b3aa321808c0afc436  rclone-v1.75.1-windows-386.zip
200eb602c126d82aa38b51e0f6b9ae837473ff99b51278d3f6f837574c494d6e  rclone-v1.75.1-windows-amd64.zip
c3c6cd0424dd49076ad179c30c3f9e5cde2c004ec07ea9fe6911f23e32eafe0f  rclone-v1.75.1-windows-arm64.zip
-----BEGIN PGP SIGNATURE-----

iF0EARECAB0WIQT79zfs6firGGBL0qyTk14C/ztU+gUCapr0vQAKCRCTk14C/ztU
+qUfAJ9PyZ/eQfhsszb9+gOHXLycJGBBEQCgvvJdc+je9kkLaPMhiycaR4Dbg/U=
=WNaw
-----END PGP SIGNATURE-----
)";

const char kWindowsLine[] = "8b4fd2737ade4e70c057ef4a49deb7cffe94b0fb9d7587c18"
                            "9d149823f21a1dc  rclone-v1.76.0-windows-amd64.zip";

QList<QByteArray> RcloneKey() {
  return {QByteArray(PgpClearsign::kRcloneReleaseKey)};
}

QList<QByteArray> TestKey() { return {QByteArray(PgpTestSigner::kPublicKey)}; }

BigUint Hex(const char *hex) {
  return BigUint::FromBytes(QByteArray::fromHex(hex));
}

} // namespace

class PgpClearsignTest : public QObject {
  Q_OBJECT

private slots:
  void bigUintMatchesPython_data() {
    QTest::addColumn<QByteArray>("a");
    QTest::addColumn<QByteArray>("m");
    QTest::addColumn<QByteArray>("remainder");
    // The first three make algorithm D's estimate one too big, so it has to
    // add the divisor back.
    QTest::newRow("add back 1")
        << QByteArray("7fffffffffffffff000000017fffffff00000000fffffffe7fffffff")
        << QByteArray("800000008000000080000000ffffffff")
        << QByteArray("80000000800000007ffffff980000002");
    QTest::newRow("add back 2")
        << QByteArray("ffffffff8000000131b1891a00000000")
        << QByteArray("ffffffff8000000180000000")
        << QByteArray("ffffffff31b1891b80000000");
    QTest::newRow("add back 3")
        << QByteArray("fffffffefffffffe800000005deed32e")
        << QByteArray("7fffffff7fffffff7fffffff730647d5")
        << QByteArray("7fffffff7fffffff00000000eae88b59");
    QTest::newRow("multi-limb")
        << QByteArray("0d58a49643f1840e3de8acfe4170651352f2935ceabb98b9464be27d8b6ed8d9b7daadc64e79649f2dad8d829730ff8c0ec7b2e342798c98920f90210034f27f336b17d38e6326ba048c5c5840bbd6846191f21ecd32d4ab")
        << QByteArray("0ec520d84c9e33a17e4b16bde349dbe0475a7e4ee40fa2da43a08671fbef1761517370253691")
        << QByteArray("0386bd4b544cc544447a058e7bae38ff5c5bdf6ad4fb64d65abfaaae7684945c9ee299750797");
  }

  void bigUintMatchesPython() {
    QFETCH(QByteArray, a);
    QFETCH(QByteArray, m);
    QFETCH(QByteArray, remainder);
    QCOMPARE(BigUint::Mod(Hex(a.constData()), Hex(m.constData())),
             Hex(remainder.constData()));
  }

  void powModMatchesPython() {
    QCOMPARE(BigUint::PowMod(Hex("04b954c0cae261b668c9110ab04a875dff24a9602f9af27149a59db7a7cc170b3d0a1deba7323e5f226b19c7f3440c9e2c2b594a5b1dc5cad508e1f557963d"),
                             Hex("41cf9251e19b81289ea5ef82fc6e53dbac686db9fef843bab8"),
                             Hex("10381d145f52b8509488e806b63ed11dda09c746f8ac1db1fa49d313310d59139e59aaddecc0cfde212532de9425be21d985c91d62a6c5953d16964f5a33c643")),
             Hex("058b14ec4521cc9e5ecf8d8b207b82107884a3347ad10d42580fac2c8a2721b9535167f7c575536ece3e5ee907c9e30da9f9f1467c60d8ffe9f17e4fc96b166d"));
    QCOMPARE(BigUint::PowMod(Hex("05"), Hex(""), Hex("07")), Hex("01"));
    QCOMPARE(BigUint::Sub(Hex("0100000000"), Hex("01")), Hex("ffffffff"));
    QCOMPARE(BigUint::Add(Hex("ffffffff"), Hex("01")), Hex("0100000000"));
    QCOMPARE(Hex("00000001ff").toBytes(), QByteArray::fromHex("01ff"));
    QCOMPARE(Hex("01ff").bitLength(), 9);
  }

  void theTestKeysNumbersAgree() {
    // g^x mod p is the public y, so the signer below signs with the key the
    // armored block describes.
    QCOMPARE(BigUint::PowMod(PgpTestSigner::G(), PgpTestSigner::X(),
                             PgpTestSigner::P()),
             PgpTestSigner::Y());
  }

  void fingerprintsMatchGnuPG() {
    QCOMPARE(PgpClearsign::Fingerprint(PgpClearsign::kRcloneReleaseKey),
             QString("FBF737ECE9F8AB18604BD2AC93935E02FF3B54FA"));
    QCOMPARE(PgpClearsign::Fingerprint(PgpTestSigner::kPublicKey),
             QString(PgpTestSigner::kFingerprint));
    QVERIFY(PgpClearsign::Fingerprint("not a key").isEmpty());
  }

  void rclonesRealListVerifies() {
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(kRealSums1760, RcloneKey());
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.text.contains(kWindowsLine));
    QVERIFY(!result.text.contains("PGP"));
    QVERIFY(result.text.startsWith("c047a1090c30086da45b7e2a91119947bb3d3e55"));

    const PgpClearsign::Verified older =
        PgpClearsign::Verify(kRealSums1751, RcloneKey());
    QVERIFY2(older.ok, qPrintable(older.error));
  }

  void windowsLineEndingsDontMatter() {
    QByteArray crlf = QByteArray(kRealSums1760).replace("\n", "\r\n");
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(crlf, RcloneKey());
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.text.contains(kWindowsLine));
    QVERIFY(!result.text.contains('\r'));
  }

  void oneChangedHashIsRefused() {
    QByteArray tampered(kRealSums1760);
    const int at = static_cast<int>(tampered.indexOf(kWindowsLine));
    QVERIFY(at > 0);
    tampered[at] = '9';
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(tampered, RcloneKey());
    QVERIFY(!result.ok);
    QVERIFY(result.text.isEmpty());
    QVERIFY2(result.error.contains("doesn't match"), qPrintable(result.error));
  }

  void aDroppedLineIsRefused() {
    QByteArray shorter(kRealSums1760);
    const int at = static_cast<int>(shorter.indexOf(kWindowsLine));
    shorter.remove(at, static_cast<int>(qstrlen(kWindowsLine)) + 1);
    QVERIFY(!PgpClearsign::Verify(shorter, RcloneKey()).ok);
  }

  void aChangedSignatureIsRefused() {
    // Without the armor checksum a flipped character reaches the DSA check.
    QByteArray changed(kRealSums1760);
    changed.replace("=dvhP\n", "");
    changed.replace("+nzmAKCmjwQrt6tn9hphj6Rq22kGSrok8ACdHXYVz1s",
                    "+nzmAKCmjwQrt6tn9hphj6Rq22kGSrok8ACdHXYVz1t");
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(changed, RcloneKey());
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains("doesn't match"), qPrintable(result.error));

    // With it, the checksum catches the change first.
    QByteArray withChecksum(kRealSums1760);
    withChecksum.replace("+nzmAKCmjwQrt6tn9hphj6Rq22kGSrok8ACdHXYVz1s",
                         "+nzmAKCmjwQrt6tn9hphj6Rq22kGSrok8ACdHXYVz1t");
    QVERIFY(!PgpClearsign::Verify(withChecksum, RcloneKey()).ok);
  }

  void aListSignedByAnotherKeyIsRefused() {
    const PgpClearsign::Verified real =
        PgpClearsign::Verify(kRealSums1760, RcloneKey());
    QVERIFY(real.ok);
    const QByteArray forged = PgpTestSigner::Clearsign(real.text);

    const PgpClearsign::Verified result =
        PgpClearsign::Verify(forged, RcloneKey());
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(PgpTestSigner::kFingerprint),
             qPrintable(result.error));

    // The same message is fine for whoever does trust that key.
    const PgpClearsign::Verified trusted =
        PgpClearsign::Verify(forged, TestKey());
    QVERIFY2(trusted.ok, qPrintable(trusted.error));
    QCOMPARE(trusted.text, real.text);
  }

  void anyTrustedKeyWillDo() {
    QList<QByteArray> both = RcloneKey() + TestKey();
    QVERIFY(PgpClearsign::Verify(kRealSums1760, both).ok);
    QVERIFY(PgpClearsign::Verify(PgpTestSigner::Clearsign(QByteArray("a\n")),
                                 both)
                .ok);
  }

  void textOutsideTheSignedBlockIsRefused() {
    const QByteArray extra =
        "0000000000000000000000000000000000000000000000000000000000000000  "
        "rclone-v1.76.0-windows-amd64.zip\n";
    const PgpClearsign::Verified after =
        PgpClearsign::Verify(QByteArray(kRealSums1760) + extra, RcloneKey());
    QVERIFY(!after.ok);
    QVERIFY2(after.error.contains("after the signature"),
             qPrintable(after.error));

    QVERIFY(!PgpClearsign::Verify(extra + kRealSums1760, RcloneKey()).ok);
    // Blank lines around it are harmless.
    QVERIFY(PgpClearsign::Verify("\n" + QByteArray(kRealSums1760) + "\n\n",
                                 RcloneKey())
                .ok);
  }

  void anUnsignedListIsRefused() {
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(QByteArray(kWindowsLine) + "\n", RcloneKey());
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains("isn't signed"), qPrintable(result.error));
    QVERIFY(!PgpClearsign::Verify(QByteArray(), RcloneKey()).ok);

    // The signed block with its signature cut off.
    QByteArray cut(kRealSums1760);
    cut.truncate(static_cast<int>(cut.indexOf("-----BEGIN PGP SIGNATURE")));
    QVERIFY(!PgpClearsign::Verify(cut, RcloneKey()).ok);
  }

  void dashEscapesAndTrailingSpacesFollowTheRfc() {
    const QList<QByteArray> lines = {"-starts with a dash", "trailing spaces   ",
                                     "", "plain"};
    QByteArray message = PgpTestSigner::Clearsign(lines);
    QVERIFY(message.contains("\n- -starts with a dash\n"));

    // Trailing whitespace isn't signed, so adding some changes nothing.
    message.replace("\nplain\n", "\nplain \t\n");
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(message, TestKey());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.text,
             QByteArray("-starts with a dash\ntrailing spaces\n\nplain"));
  }

  void aKeyThatIsntDsaIsReported() {
    const PgpClearsign::Verified result =
        PgpClearsign::Verify(kRealSums1760, {QByteArray("garbage")});
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains("trusted key"), qPrintable(result.error));
  }
};

QTEST_GUILESS_MAIN(PgpClearsignTest)
#include "pgp_clearsign_test.moc"
