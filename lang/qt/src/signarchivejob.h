/*
    signarchivejob.h

    This file is part of qgpgme, the Qt API binding for gpgme
    Copyright (c) 2023 g10 Code GmbH
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

#ifndef __QGPGME_SIGNARCHIVEJOB_H__
#define __QGPGME_SIGNARCHIVEJOB_H__

#include "job.h"

#ifdef BUILDING_QGPGME
# include "context.h"
#else
# include <gpgme++/context.h>
#endif

namespace GpgME
{
class Key;
}

namespace QGpgME
{

/**
 * Abstract base class for job for creating signed archives
 */
class QGPGME_EXPORT SignArchiveJob : public Job
{
    Q_OBJECT
protected:
    explicit SignArchiveJob(QObject *parent);
public:
    ~SignArchiveJob() override;

    static bool isSupported();

    void setBaseDirectory(const QString &baseDirectory);
    QString baseDirectory() const;

    /**
     * Starts the creation of a signed archive.
     *
     * Creates a signed archive with the files and directories in \a paths.
     * The archive is signed with the keys in \a signers or with the default
     * key, if \a signers is empty. The signed archive is written to \a output.
     *
     * Emits result() when the job has finished.
     */
    virtual GpgME::Error start(const std::vector<GpgME::Key> &signers,
                               const std::vector<QString> &paths,
                               const std::shared_ptr<QIODevice> &output) = 0;

Q_SIGNALS:
    void result(const GpgME::SigningResult &result,
                const QString &auditLogAsHtml = {},
                const GpgME::Error &auditLogError = {});
};

}

#endif // __QGPGME_SIGNARCHIVEJOB_H__