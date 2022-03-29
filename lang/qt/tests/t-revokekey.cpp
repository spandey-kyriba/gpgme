/* t-revokekey.cpp

    This file is part of qgpgme, the Qt API binding for gpgme
    Copyright (c) 2022 g10 Code GmbH
    Software engineering by Ingo Klöcker <dev@ingo-kloecker.de>

    QGpgME is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.

    QGpgME is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    In addition, as a special exception, the copyright holders give
    permission to link the code of this program with any edition of
    the Qt library by Trolltech AS, Norway (or with modified versions
    of Qt that use the same license as Qt), and distribute linked
    combinations including the two.  You must obey the GNU General
    Public License in all respects for all of the code used other than
    Qt.  If you modify this file, you may extend this exception to
    your version of the file, but you are not obligated to do so.  If
    you do not wish to do so, delete this exception statement from
    your version.
*/
#ifdef HAVE_CONFIG_H
 #include "config.h"
#endif

#include "t-support.h"

#include <protocol.h>
#include <revokekeyjob.h>

#include <QSignalSpy>
#include <QTest>

#include <context.h>
#include <data.h>

#include <algorithm>

using namespace QGpgME;
using namespace GpgME;

/* Test keys
    sec   ed25519 2022-03-29 [SC]
        604122B94C86BE846EAFE637FC2BCFB1B19A1CF4
    uid           [ultimate] revoke-me@example.net
    ssb   cv25519 2022-03-29 [E]
 * generated with
export GNUPGHOME=$(mktemp -d)
gpg -K
gpg --batch --pinentry-mode loopback --passphrase abc --quick-gen-key revoke-me@example.net default default never
gpg -K
gpg --export-secret-keys --armor --batch --pinentry-mode loopback --passphrase abc --comment revoke-me@example.net revoke-me@example.net | sed 's/\(.*\)/    "\1\\n"/'
#rm -rf ${GNUPGHOME}
unset GNUPGHOME
*/
static const char *testKeyData =
    "-----BEGIN PGP PRIVATE KEY BLOCK-----\n"
    "Comment: revoke-me@example.net\n"
    "\n"
    "lIYEYkLSGhYJKwYBBAHaRw8BAQdAWKBjYOZIW33CjwlHKKGIgqXDOGhmbPCStkj1\n"
    "+2/cVFL+BwMCXJpRHkD8EcT8DMWdVo84Lx4w7RNDCQx5xnm6rO5kvtmh+PjgM3qt\n"
    "CQVGy8H7Dq35yzi0Hihm5zvHxVGYdAu96ShAI2ZqqVL7is0CdAmAibQVcmV2b2tl\n"
    "LW1lQGV4YW1wbGUubmV0iJQEExYKADwWIQRgQSK5TIa+hG6v5jf8K8+xsZoc9AUC\n"
    "YkLSGgIbAwULCQgHAgMiAgEGFQoJCAsCBBYCAwECHgcCF4AACgkQ/CvPsbGaHPSH\n"
    "LAD/RNFgm1Bp6ltDXLS6oS0S5Bgjjg3CBpbdxWTvLjPpaagBAIU2pTLrsGNDKIZq\n"
    "EAY7hY50tdcvOfT4OSAySJACJzMFnIsEYkLSGhIKKwYBBAGXVQEFAQEHQIOTbPEz\n"
    "hUtL72BHfetUWESlEbh2IF/NEUWASUtQJDghAwEIB/4HAwJGE5naBnwwcfyPC+Nq\n"
    "DwY5FO28hQVAzgNu9KAncmPtpST1J8sEPAtJGhtq/9fki9eSvBMbAa64VVpFHKHK\n"
    "ravZxr2uCrK6J/u4rTvnR8HgiHgEGBYKACAWIQRgQSK5TIa+hG6v5jf8K8+xsZoc\n"
    "9AUCYkLSGgIbDAAKCRD8K8+xsZoc9ANAAP9rX/xanm7YvcGFIxPclmy4h33lLaG8\n"
    "dE5RA6zeSg7DqQD8Dae82iKaqKfTpe2+2vIEyxBVy8+WttoElUoXiwr0AQg=\n"
    "=/5re\n"
    "-----END PGP PRIVATE KEY BLOCK-----\n";

class RevokeKeyJobTest : public QGpgMETest
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        QGpgMETest::initTestCase();

        // set up the test fixture for this test
        qputenv("GNUPGHOME", mGnupgHomeTestFixture.path().toUtf8());
        QVERIFY(importSecretKeys(testKeyData, 1));
    }

    void init()
    {
        // set up a copy of the test fixture for each test function
        mGnupgHomeTestCopy.reset(new QTemporaryDir{});
        QVERIFY(copyKeyrings(mGnupgHomeTestFixture.path(), mGnupgHomeTestCopy->path()));
        qputenv("GNUPGHOME", mGnupgHomeTestCopy->path().toUtf8());
    }

    void testAsync()
    {
        // Get the key that shall be revoked
        auto key = getTestKey("revoke-me@example.net");
        QVERIFY(!key.isNull());
        QVERIFY(!key.isRevoked());

        auto job = std::unique_ptr<RevokeKeyJob>{openpgp()->revokeKeyJob()};
        hookUpPassphraseProvider(job.get());

        Error result;
        connect(job.get(), &RevokeKeyJob::result,
                job.get(), [this, &result](const Error &result_) {
                    result = result_;
                    Q_EMIT asyncDone();
                });
        QVERIFY(!job->start(key, RevocationReason::NoLongerUsed,
                            {"This key is not used anymore."}));
        job.release(); // after the job has been started it's on its own

        QSignalSpy spy (this, SIGNAL(asyncDone()));
        QVERIFY(spy.wait(QSIGNALSPY_TIMEOUT));

        QVERIFY(result.code() == GPG_ERR_NO_ERROR);
        key.update();
        QVERIFY(key.isRevoked());
    }

    void testSync_noReasonDescription()
    {
        // Get the key that shall be revoked
        auto key = getTestKey("revoke-me@example.net");
        QVERIFY(!key.isNull());
        QVERIFY(!key.isRevoked());

        auto job = std::unique_ptr<RevokeKeyJob>{openpgp()->revokeKeyJob()};
        hookUpPassphraseProvider(job.get());

        const auto result = job->exec(key);

        QVERIFY(result.code() == GPG_ERR_NO_ERROR);
        key.update();
        QVERIFY(key.isRevoked());
    }

    void testSync_oneLineReasonDescription()
    {
        // Get the key that shall be revoked
        auto key = getTestKey("revoke-me@example.net");
        QVERIFY(!key.isNull());
        QVERIFY(!key.isRevoked());

        auto job = std::unique_ptr<RevokeKeyJob>{openpgp()->revokeKeyJob()};
        hookUpPassphraseProvider(job.get());

        const auto result = job->exec(key, RevocationReason::Compromised,
                                      {"The secret key was stolen."});

        QVERIFY(result.code() == GPG_ERR_NO_ERROR);
        key.update();
        QVERIFY(key.isRevoked());
    }

    void testSync_twoLinesReasonDescription()
    {
        // Get the key that shall be revoked
        auto key = getTestKey("revoke-me@example.net");
        QVERIFY(!key.isNull());
        QVERIFY(!key.isRevoked());

        auto job = std::unique_ptr<RevokeKeyJob>{openpgp()->revokeKeyJob()};
        hookUpPassphraseProvider(job.get());

        const auto result = job->exec(key, RevocationReason::Superseded,
                                      {"This key has been superseded by key",
                                       "0000 1111 2222 3333 4444 5555 6666 7777 8888 9999."});

        QVERIFY(result.code() == GPG_ERR_NO_ERROR);
        key.update();
        QVERIFY(key.isRevoked());
    }

private:
    Key getTestKey(const char *pattern)
    {
        auto ctx = Context::create(OpenPGP);
        VERIFY_OR_OBJECT(ctx);

        Error err;
        auto key = ctx->key(pattern, err, /*secret=*/true);
        VERIFY_OR_OBJECT(!err);
        VERIFY_OR_OBJECT(!key.isNull());
        return key;
    }

private:
    QTemporaryDir mGnupgHomeTestFixture;
    std::unique_ptr<QTemporaryDir> mGnupgHomeTestCopy;
};

QTEST_MAIN(RevokeKeyJobTest)

#include "t-revokekey.moc"
