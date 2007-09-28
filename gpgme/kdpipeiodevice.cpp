/*
  Copyright (C) 2007 Klar�lvdalens Datakonsult AB

  KDPipeIODevice is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  KDPipeIODevice is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Library General Public License for more details.

  You should have received a copy of the GNU Library General Public License
  along with KDPipeIODevice; see the file COPYING.LIB.  If not, write to the
  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.
*/

#include "kdpipeiodevice.h"

#include <QtCore>

#include <cassert>
#include <memory>
#include <algorithm>

#ifdef Q_OS_WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <io.h>
#else
# include <unistd.h>
# include <errno.h>
#endif

using namespace _gpgme_;

#ifndef KDAB_CHECK_THIS
# define KDAB_CHECK_CTOR (void)1
# define KDAB_CHECK_DTOR KDAB_CHECK_CTOR
# define KDAB_CHECK_THIS KDAB_CHECK_CTOR
#endif

#define LOCKED( d ) const QMutexLocker locker( &d->mutex )
#define synchronized( d ) if ( int i = 0 ) {} else for ( const QMutexLocker locker( &d->mutex ) ; !i ; ++i )

const unsigned int BUFFER_SIZE = 4096;
const bool ALLOW_QIODEVICE_BUFFERING = true;

// comment to get trace output:
//#define qDebug if(1){}else qDebug

namespace {
class Reader : public QThread {
    Q_OBJECT
public:
    Reader( int fd, Qt::HANDLE handle );
    ~Reader();

    qint64 readData( char * data, qint64 maxSize );

    unsigned int bytesInBuffer() const {
        return ( wptr + sizeof buffer - rptr ) % sizeof buffer ;
    }

    bool bufferFull() const {
        return bytesInBuffer() == sizeof buffer - 1;
    }

    bool bufferEmpty() const {
	return bytesInBuffer() == 0;
    }

    bool bufferContains( char ch ) {
	const unsigned int bib = bytesInBuffer();
	for ( unsigned int i = rptr ; i < rptr + bib ; ++i )
	    if ( buffer[i%sizeof buffer] == ch )
		return true;
	return false;
    }
        
Q_SIGNALS:
    void readyRead();

protected:
    /* reimp */ void run();

private:
    int fd;
    Qt::HANDLE handle;
public:
    QMutex mutex;
    QWaitCondition bufferNotFullCondition;
    QWaitCondition bufferNotEmptyCondition;
    QWaitCondition hasStarted;
    bool cancel;
    bool eof;
    bool error;
    bool eofShortCut;
    int errorCode;
private:
    unsigned int rptr, wptr;
    char buffer[BUFFER_SIZE+1]; // need to keep one byte free to detect empty state
};


Reader::Reader( int fd_, Qt::HANDLE handle_ )
    : QThread(),
      fd( fd_ ),
      handle( handle_ ),
      mutex(),
      bufferNotFullCondition(),
      bufferNotEmptyCondition(),
      hasStarted(),
      cancel( false ),
      eof( false ),
      error( false ),
      eofShortCut( false ),
      errorCode( 0 ),
      rptr( 0 ), wptr( 0 )
{
    
}

Reader::~Reader() {}


class Writer : public QThread {
    Q_OBJECT
public:
    Writer( int fd, Qt::HANDLE handle );
    ~Writer();

    qint64 writeData( const char * data, qint64 size );

    unsigned int bytesInBuffer() const { return numBytesInBuffer; }

    bool bufferFull() const {
        return numBytesInBuffer == sizeof buffer;
    }

    bool bufferEmpty() const {
	return numBytesInBuffer == 0;
    }

Q_SIGNALS:
    void bytesWritten( qint64 );

protected:
    /* reimp */ void run();

private:
    int fd;
    Qt::HANDLE handle;
public:
    QMutex mutex;
    QWaitCondition bufferEmptyCondition;
    QWaitCondition bufferNotEmptyCondition;
    QWaitCondition hasStarted;
    bool cancel;
    bool error;
    int errorCode;
private:
    unsigned int numBytesInBuffer;
    char buffer[BUFFER_SIZE];
};
}

Writer::Writer( int fd_, Qt::HANDLE handle_ )
    : QThread(),
      fd( fd_ ),
      handle( handle_ ),
      mutex(),
      bufferEmptyCondition(),
      bufferNotEmptyCondition(),
      hasStarted(),
      cancel( false ),
      error( false ),
      errorCode( 0 ),
      numBytesInBuffer( 0 )
{

}

Writer::~Writer() {}


class KDPipeIODevice::Private : public QObject {
Q_OBJECT
    friend class ::KDPipeIODevice;
    KDPipeIODevice * const q;
public:
    explicit Private( KDPipeIODevice * qq );
    ~Private();

    bool doOpen( int, Qt::HANDLE, OpenMode );
    bool startReaderThread(); 
    bool startWriterThread(); 
    void stopThreads();
    bool triedToStartReader;
    bool triedToStartWriter;

private:
    int fd;
    Qt::HANDLE handle;
    Reader * reader;
    Writer * writer;
};

KDPipeIODevice::Private::Private( KDPipeIODevice * qq )
    : QObject( qq ), q( qq ),
      fd( -1 ),
      handle( 0 ),
      reader( 0 ),
      writer( 0 ),
      triedToStartReader( false ), triedToStartWriter( false )
{

}

KDPipeIODevice::Private::~Private() {}

KDPipeIODevice::KDPipeIODevice( QObject * p )
    : QIODevice( p ), d( new Private( this ) )
{
    KDAB_CHECK_CTOR;
}

KDPipeIODevice::KDPipeIODevice( int fd, OpenMode mode, QObject * p )
    : QIODevice( p ), d( new Private( this ) )
{
    KDAB_CHECK_CTOR;
    open( fd, mode );
}

KDPipeIODevice::KDPipeIODevice( Qt::HANDLE handle, OpenMode mode, QObject * p )
    : QIODevice( p ), d( new Private( this ) )
{
    KDAB_CHECK_CTOR;
    open( handle, mode );
}

KDPipeIODevice::~KDPipeIODevice() { KDAB_CHECK_DTOR;
    if ( isOpen() )
	close();
    delete d; d = 0;
}


bool KDPipeIODevice::open( int fd, OpenMode mode ) { KDAB_CHECK_THIS;

#ifdef Q_OS_WIN32
    return d->doOpen( fd, (HANDLE)_get_osfhandle( fd ), mode );
#else
    return d->doOpen( fd, 0, mode );
#endif

}

bool KDPipeIODevice::open( Qt::HANDLE h, OpenMode mode ) { KDAB_CHECK_THIS;

#ifdef Q_OS_WIN32
    return d->doOpen( 0, h, mode );
#else
    Q_UNUSED( h );
    Q_UNUSED( mode );
    assert( !"KDPipeIODevice::open( Qt::HANDLE, OpenMode ) should never be called except on Windows." );
#endif

}

bool KDPipeIODevice::Private::startReaderThread()
{
   if ( triedToStartReader )
       return true;
   triedToStartReader = true;    
   if ( reader && !reader->isRunning() && !reader->isFinished() ) {
       LOCKED( reader );
       
       reader->start( QThread::HighestPriority );
       if ( !reader->hasStarted.wait( &reader->mutex, 1000 ) )
            return false;
   }
   return true;
}

bool KDPipeIODevice::Private::startWriterThread()
{
   if ( triedToStartWriter )
       return true;
   triedToStartWriter = true;    
   if ( writer && !writer->isRunning() && !writer->isFinished() ) {
       LOCKED( writer );
       
       writer->start( QThread::HighestPriority );
       if ( !writer->hasStarted.wait( &writer->mutex, 1000 ) )
            return false;
   }
   return true;
}

bool KDPipeIODevice::Private::doOpen( int fd_, Qt::HANDLE handle_, OpenMode mode_ ) {

    if ( q->isOpen() || fd_ < 0 )
	return false;

#ifdef Q_OS_WIN32
    if ( !handle_ )
	return false;
#endif

    if ( !(mode_ & ReadWrite) )
	return false; // need to have at least read -or- write

    fd = fd_;
    handle = handle_;

    std::auto_ptr<Reader> reader_;
    std::auto_ptr<Writer> writer_;

    if ( mode_ & ReadOnly ) {
	reader_.reset( new Reader( fd_, handle_ ) );
        qDebug( "KDPipeIODevice::doOpen: created reader for fd %d", fd_ ); 
	connect( reader_.get(), SIGNAL(readyRead()), q, 
SIGNAL(readyRead()), Qt::QueuedConnection );
    }
    if ( mode_ & WriteOnly ) {
	writer_.reset( new Writer( fd_, handle_ ) );
        qDebug( "KDPipeIODevice::doOpen: created writer for fd %d", fd_ ); 
        connect( writer_.get(), SIGNAL(bytesWritten(qint64)), q, SIGNAL(bytesWritten(qint64)), 
Qt::QueuedConnection );
    }

    // commit to *this:
    fd = fd_;
    handle = handle_;
    reader = reader_.release();
    writer = writer_.release();

    q->setOpenMode( mode_|Unbuffered );
    return true;
}

int KDPipeIODevice::descriptor() const { KDAB_CHECK_THIS;
    return d->fd;
}


Qt::HANDLE KDPipeIODevice::handle() const { KDAB_CHECK_THIS;
    return d->handle;
}

qint64 KDPipeIODevice::bytesAvailable() const { KDAB_CHECK_THIS;
    const qint64 base = QIODevice::bytesAvailable();
    if ( !d->triedToStartReader ) {
         d->startReaderThread();
         return base;
    }
    if ( d->reader )
	synchronized( d->reader ) return base + d->reader->bytesInBuffer();
    return base;
}

qint64 KDPipeIODevice::bytesToWrite() const { KDAB_CHECK_THIS;
    d->startWriterThread();
    const qint64 base = QIODevice::bytesToWrite();
    if ( d->writer )
	synchronized( d->writer ) return base + d->writer->bytesInBuffer();
    return base;
}

bool KDPipeIODevice::canReadLine() const { KDAB_CHECK_THIS;
    d->startReaderThread();
   if ( QIODevice::canReadLine() )
	return true;
    if ( d->reader )
	synchronized( d->reader ) return d->reader->bufferContains( '\n' );
    return true;
}

bool KDPipeIODevice::isSequential() const {
    return true;
}

bool KDPipeIODevice::atEnd() const { KDAB_CHECK_THIS;
    d->startReaderThread();
    if ( !QIODevice::atEnd() ) {
	qDebug( "%p: KDPipeIODevice::atEnd returns false since QIODevice::atEnd does (with bytesAvailable=%ld)", this, static_cast<long>(bytesAvailable()) );
	return false;
    }
    if ( !isOpen() )
	return true;
    if ( d->reader->eofShortCut )
	return true;
    LOCKED( d->reader );
    const bool eof = ( d->reader->error || d->reader->eof ) && d->reader->bufferEmpty();
    if ( !eof ) {
	if ( !d->reader->error && !d->reader->eof )
	    qDebug( "%p: KDPipeIODevice::atEnd returns false since !reader->error && !reader->eof", this );
	if ( !d->reader->bufferEmpty() )
	    qDebug( "%p: KDPipeIODevice::atEnd returns false since !reader->bufferEmpty()", this );
    }
    return eof;
}

bool KDPipeIODevice::waitForBytesWritten( int msecs ) { KDAB_CHECK_THIS;
    d->startWriterThread();
    Writer * const w = d->writer;
    if ( !w )
	return true;
    LOCKED( w );
    return w->bufferEmpty() || w->error || w->bufferEmptyCondition.wait( &w->mutex, msecs ) ;
}

bool KDPipeIODevice::waitForReadyRead( int msecs ) { KDAB_CHECK_THIS;
    d->startReaderThread();
    if ( ALLOW_QIODEVICE_BUFFERING ) {
	if ( bytesAvailable() > 0 )
	    return true;
    }
    Reader * const r = d->reader;
    if ( !r || r->eofShortCut )
	return true;
    LOCKED( r );
    return r->bytesInBuffer() != 0 || r->eof || r->error || r->bufferNotEmptyCondition.wait( &r->mutex, msecs ) ;
}

qint64 KDPipeIODevice::readData( char * data, qint64 maxSize ) { KDAB_CHECK_THIS;
    qDebug( "%p: KDPipeIODevice::readData: data=%p, maxSize=%lld", this, data, maxSize );

    if ( maxSize == 0 )
        return 0;
    d->startReaderThread();

    Reader * const r = d->reader;

    assert( r );

    //assert( r->isRunning() ); // wrong (might be eof, error)
    assert( data || maxSize == 0 );
    assert( maxSize >= 0 );

    if ( r->eofShortCut ) {
	qDebug( "%p: KDPipeIODevice::readData: hit eofShortCut, returning 0", this );
	return 0;
    }

    if ( maxSize < 0 )
	maxSize = 0;

    if ( ALLOW_QIODEVICE_BUFFERING ) {
	if ( bytesAvailable() > 0 )
	    maxSize = std::min( maxSize, bytesAvailable() ); // don't block
    }

    LOCKED( r );
    if ( /* maxSize > 0 && */ r->bufferEmpty() && !r->error && !r->eof ) { // ### block on maxSize == 0?
	qDebug( "%p: KDPipeIODevice::readData: waiting for bufferNotEmptyCondition", this );
	r->bufferNotEmptyCondition.wait( &r->mutex );
    }

    if ( r->bufferEmpty() ) {
	qDebug( "%p: KDPipeIODevice::readData: got empty buffer, signal eof", this );
	// woken with an empty buffer must mean either EOF or error:
	assert( r->eof || r->error );
	r->eofShortCut = true;
	return r->eof ? 0 : -1 ;
    }

    qDebug( "%p: KDPipeIODevice::readData: got bufferNotEmptyCondition, trying to read %lld bytes", this, maxSize );
    const qint64 bytesRead = r->readData( data, maxSize );
    qDebug( "%p: KDPipeIODevice::readData: read %lld bytes", this, bytesRead );
    qDebug( "%p (fd=%d): KDPipeIODevice::readData: %s", this, d->fd, data );
    return bytesRead;
}

qint64 Reader::readData( char * data, qint64 maxSize ) {
    qint64 numRead = rptr < wptr ? wptr - rptr : sizeof buffer - rptr ;
    if ( numRead > maxSize )
	numRead = maxSize;

    qDebug( "%p: KDPipeIODevice::readData: data=%p, maxSize=%lld; rptr=%u, wptr=%u (bytesInBuffer=%u); -> numRead=%lld", this,
	    data, maxSize, rptr, wptr, bytesInBuffer(), numRead );

    std::memcpy( data, buffer + rptr, numRead );

    rptr = ( rptr + numRead ) % sizeof buffer ;

    if ( !bufferFull() ) {
	qDebug( "%p: KDPipeIODevice::readData: signal bufferNotFullCondition", this );
	bufferNotFullCondition.wakeAll();
    }

    return numRead;
}

qint64 KDPipeIODevice::writeData( const char * data, qint64 size ) { KDAB_CHECK_THIS;
    d->startWriterThread();
    Writer * const w = d->writer;

    assert( w );
    assert( w->error || w->isRunning() );
    assert( data || size == 0 );
    assert( size >= 0 );

    LOCKED( w );

    while ( !w->error && !w->bufferEmpty() )
	w->bufferEmptyCondition.wait( &w->mutex );

    if ( w->error )
	return -1;

    assert( w->bufferEmpty() );

    return w->writeData( data, size );
}

qint64 Writer::writeData( const char * data, qint64 size ) {
    assert( bufferEmpty() );

    if ( size > static_cast<qint64>( sizeof buffer ) )
	size = sizeof buffer;

    std::memcpy( buffer, data, size );
    
    numBytesInBuffer = size;

    if ( !bufferEmpty() )
	bufferNotEmptyCondition.wakeAll();

    return size;
}

void KDPipeIODevice::Private::stopThreads()
{
    if ( triedToStartWriter )
    {
        if ( writer && q->bytesToWrite() > 0 )
	    q->waitForBytesWritten( -1 );

        assert( q->bytesToWrite() == 0 );
    }
    if ( Reader * & r = reader ) {
	synchronized( r ) {
	    // tell thread to cancel:
	    r->cancel = true;
	    // and wake it, so it can terminate:
	    r->bufferNotFullCondition.wakeAll();
	}
    }
    if ( Writer * & w = writer ) {
	synchronized( w ) {
	    // tell thread to cancel:
	    w->cancel = true;
	    // and wake it, so it can terminate:
	    w->bufferNotEmptyCondition.wakeAll();
	}
    }
}

void KDPipeIODevice::close() { KDAB_CHECK_THIS;

    if ( !isOpen() )
	return;

    // tell clients we're about to close:
    emit aboutToClose();
    d->stopThreads();

#define waitAndDelete( t ) if ( t ) { t->wait(); delete t; t = 0; }
    waitAndDelete( d->writer );
    waitAndDelete( d->reader );
#undef waitAndDelete

#ifdef Q_OS_WIN32
    CloseHandle( d->handle );
#else
    ::close( d->fd );
#endif

    setOpenMode( NotOpen );
    d->fd = -1;
    d->handle = 0;
}

void Reader::run() {

    LOCKED( this );

    // too bad QThread doesn't have that itself; a signal isn't enough
    hasStarted.wakeAll();

    qDebug( "%p: Reader::run: started", this );

    while ( true ) {

	while ( !cancel && bufferFull() ) {
	    bufferNotEmptyCondition.wakeAll();
	    qDebug( "%p: Reader::run: buffer is full, going to sleep", this );
	    bufferNotFullCondition.wait( &mutex );
	    qDebug( "%p: Reader::run: woke up", this );
	}

	if ( cancel ) {
	    qDebug( "%p: Reader::run: detected cancel", this );
	    goto leave;
	}

	if ( rptr == wptr ) // optimize for larger chunks in case the buffer is empty
	    rptr = wptr = 0;

	unsigned int numBytes = ( rptr + sizeof buffer - wptr - 1 ) % sizeof buffer;
	if ( numBytes > sizeof buffer - wptr )
	    numBytes = sizeof buffer - wptr;

	qDebug( "%p: Reader::run: rptr=%d, wptr=%d -> numBytes=%d", this, rptr, wptr, numBytes );

	assert( numBytes > 0 );

	qDebug( "%p: Reader::run: trying to read %d bytes", this, numBytes );
#ifdef Q_OS_WIN32
	DWORD numRead;
	mutex.unlock();
	const bool ok = ReadFile( handle, buffer + wptr, numBytes, &numRead, 0 );
	mutex.lock();
	if ( !ok ) {
	    errorCode = static_cast<int>( GetLastError() );
	    if ( errorCode == ERROR_BROKEN_PIPE ) {
		qDebug( "%p: Reader::run: got eof (broken pipe)", this );
		eof = true;
	    } else {
		qDebug( "%p: Reader::run: got error: %s (%d)", this, strerror( errorCode ), errorCode );
		error = true;
	    }
	    goto leave;
	}
#else
	qint64 numRead;
	mutex.unlock();
	do {
	    numRead = ::read( fd, buffer + wptr, numBytes );
	} while ( numRead == -1 && errno == EINTR );
	mutex.lock();

	if ( numRead < 0 ) {
	    errorCode = errno;
	    error = true;
	    qDebug( "%p: Reader::run: got error: %d", this, errorCode );
	    goto leave;
	}
#endif
	qDebug( "%p: Reader::run: read %ld bytes", this, static_cast<long>(numRead) );
	qDebug( "%p (fd=%d): KDPipeIODevice::readData: %s", this, fd, buffer );
	if ( numRead == 0 ) {
	    qDebug( "%p: Reader::run: eof detected", this );
	    eof = true;
	    goto leave;
	}

	if ( cancel ) {
	    qDebug( "%p: Reader::run: detected cancel", this );
	    goto leave;
	}
	qDebug( "%p: Reader::run: buffer before: rptr=%4d, wptr=%4d", this, rptr, wptr );
	wptr = ( wptr + numRead ) % sizeof buffer;
	qDebug( "%p: Reader::run: buffer after:  rptr=%4d, wptr=%4d", this, rptr, wptr );
	if ( !bufferEmpty() ) {
	    qDebug( "%p: Reader::run: buffer no longer empty, waking everyone", this );
	    bufferNotEmptyCondition.wakeAll();
	    emit readyRead();
	}
    }
 leave:
    qDebug( "%p: Reader::run: terminating", this );
    bufferNotEmptyCondition.wakeAll();
    emit readyRead();
}

void Writer::run() {

    LOCKED( this );

    // too bad QThread doesn't have that itself; a signal isn't enough
    hasStarted.wakeAll();

    qDebug( "%p: Writer::run: started", this );

    while ( true ) {

	while ( !cancel && bufferEmpty() ) {
	    bufferEmptyCondition.wakeAll();
	    qDebug( "%p: Writer::run: buffer is empty, going to sleep", this );
	    bufferNotEmptyCondition.wait( &mutex );
	    qDebug( "%p: Writer::run: woke up", this );
	}

	if ( cancel ) {
	    qDebug( "%p: Writer::run: detected cancel", this );
	    goto leave;
	}

	assert( numBytesInBuffer > 0 );

	qDebug( "%p: Writer::run: Trying to write %u bytes", this, numBytesInBuffer );
	qint64 totalWritten = 0;
	do { 
	    mutex.unlock();
#ifdef Q_OS_WIN32
	    DWORD numWritten;
	    if ( !WriteFile( handle, buffer + totalWritten, numBytesInBuffer - totalWritten, &numWritten, 0 ) ) {
		mutex.lock();
		errorCode = static_cast<int>( GetLastError() );
		qDebug( "%p: Writer::run: got error code: %d", this, errorCode );
		error = true;
		goto leave;
	    }
#else
	    qint64 numWritten;
	    do {
		numWritten = ::write( fd, buffer + totalWritten, numBytesInBuffer - totalWritten );
	    } while ( numWritten == -1 && errno == EINTR );

	    if ( numWritten < 0 ) {
		mutex.lock();
		errorCode = errno;
		qDebug( "%p: Writer::run: got error code: %d", this, errorCode );
		error = true;
		goto leave;
	    }
#endif
	    totalWritten += numWritten;
	    mutex.lock();
	} while ( totalWritten < numBytesInBuffer );

	qDebug( "%p: Writer::run: wrote %lld bytes", this, totalWritten );

	numBytesInBuffer = 0;
	bufferEmptyCondition.wakeAll();
	emit bytesWritten( totalWritten );
    }
 leave:
    qDebug( "%p: Writer::run: terminating", this );
    numBytesInBuffer = 0;
    bufferEmptyCondition.wakeAll();
    emit bytesWritten( 0 );
}

// static 
std::pair<KDPipeIODevice*,KDPipeIODevice*> KDPipeIODevice::makePairOfConnectedPipes() {
    KDPipeIODevice * read = 0;
    KDPipeIODevice * write = 0;
#ifdef Q_OS_WIN32
    HANDLE rh;
    HANDLE wh;
    SECURITY_ATTRIBUTES sa;
    memset( &sa, 0, sizeof(sa) );
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
	if ( CreatePipe( &rh, &wh, &sa, BUFFER_SIZE ) ) {
	read = new KDPipeIODevice;
	read->open( rh, ReadOnly );
	write = new KDPipeIODevice;
	write->open( wh, WriteOnly );
    }
#else
    int fds[2];
    if ( pipe( fds ) == 0 ) {
	read = new KDPipeIODevice;
	read->open( fds[0], ReadOnly );
	write = new KDPipeIODevice;
	write->open( fds[1], WriteOnly );
    }
#endif
    return std::make_pair( read, write );
}

#ifdef KDAB_DEFINE_CHECKS
KDAB_DEFINE_CHECKS( KDPipeIODevice ) {
    if ( !isOpen() ) {
	assert( openMode() == NotOpen );
	assert( !d->reader );
	assert( !d->writer );
#ifdef Q_OS_WIN32
	assert( !d->handle );
#else
	assert( d->fd < 0 );
#endif
    } else {
	assert( openMode() != NotOpen );
	assert( openMode() & ReadWrite );
	if ( openMode() & ReadOnly ) {
	    assert( d->reader );
	    synchronized( d->reader )
		assert( d->reader->eof || d->reader->error || d->reader->isRunning() );
	}
	if ( openMode() & WriteOnly ) {
	    assert( d->writer );
	    synchronized( d->writer )
		assert( d->writer->error || d->writer->isRunning() );
	}
#ifdef Q_OS_WIN32
	assert( d->handle );
#else
	assert( d->fd >= 0 );
#endif
    }
}
#endif // KDAB_DEFINE_CHECKS

#include "moc_kdpipeiodevice.cpp"
#include "kdpipeiodevice.moc"