#include "chunks.h"
#include <limits.h>

#define NORMAL 0
#define HIGHLIGHTED 1

#define BUFFER_SIZE 0x10000
#define CHUNK_SIZE 0x1000
#define READ_CHUNK_MASK Q_INT64_C(0xfffffffffffff000)

// ***************************************** Constructors and file settings

Chunks::Chunks(QObject *parent): QObject(parent)
{
    QBuffer *buf = new QBuffer(this);
    setIODevice(*buf);
}

Chunks::Chunks(QIODevice &ioDevice, QObject *parent): QObject(parent)
{
    setIODevice(ioDevice);
}

bool Chunks::setIODevice(QIODevice &ioDevice)
{
    _ioDevice = &ioDevice;

    bool ok = _ioDevice->open(QIODevice::ReadOnly);

    if (ok)   // Try to open IODevice
    {
        _size = _ioDevice->size();
        _ioDevice->close();
    }
    else                                        // Fallback is an empty buffer
    {
        QBuffer *buf = new QBuffer(this);
        _ioDevice = buf;
        _size = 0;
    }

    _chunks.clear();
    _pos = 0;

    return ok;
}


// ***************************************** Getting data out of Chunks

QByteArray Chunks::data(qint64 pos, qint64 maxSize, QByteArray *highlighted)
{
    qint64 ioDelta = 0;
    int chunkIdx = 0;

    Chunk chunk;
    QByteArray buffer;

    // Do some checks and some arrangements
    if (highlighted)
        highlighted->clear();

    if (pos >= _size)
        return buffer;

    if (maxSize < 0)
        maxSize = _size;
    else
        if ((pos + maxSize) > _size)
            maxSize = _size - pos;

    // Pre-allocate buffer to avoid repeated reallocations
    buffer.reserve(static_cast<int>(qMin(maxSize, qint64(INT_MAX))));
    if (highlighted)
        highlighted->reserve(static_cast<int>(qMin(maxSize, qint64(INT_MAX))));

    _ioDevice->open(QIODevice::ReadOnly);

    while (maxSize > 0)
    {
        chunk.absPos = LLONG_MAX;
        bool chunksLoopOngoing = true;

        while ((chunkIdx < _chunks.count()) && chunksLoopOngoing)
        {
            // In this section, we track changes before our required data and
            // we take the editdet data, if availible. ioDelta is a difference
            // counter to justify the read pointer to the original data, if
            // data in between was deleted or inserted.

            const Chunk &chunkRef = _chunks[chunkIdx];
            chunk.absPos = chunkRef.absPos;
            if (chunkRef.absPos > pos)
                chunksLoopOngoing = false;
            else
            {
                qint64 count;
                qint64 chunkOfs = pos - chunkRef.absPos;
                if (maxSize > ((qint64)chunkRef.data.size() - chunkOfs))
                {
                    count = (qint64)chunkRef.data.size() - chunkOfs;
                    ioDelta += CHUNK_SIZE - chunkRef.data.size();
                }
                else
                    count = maxSize;
                if (count > 0)
                {
                    buffer += chunkRef.data.mid(chunkOfs, (int)count);
                    maxSize -= count;
                    pos += count;
                    if (highlighted)
                        *highlighted += chunkRef.dataChanged.mid(chunkOfs, (int)count);
                }
                chunkIdx += 1;
            }
        }

        if ((maxSize > 0) && (pos < chunk.absPos))
        {
            // In this section, we read data from the original source. This only will
            // happen, whe no copied data is available

            qint64 byteCount;
            QByteArray readBuffer;
            if ((chunk.absPos - pos) > maxSize)
                byteCount = maxSize;
            else
                byteCount = chunk.absPos - pos;

            maxSize -= byteCount;
            _ioDevice->seek(pos + ioDelta);
            readBuffer = _ioDevice->read(byteCount);
            buffer += readBuffer;
            if (highlighted)
                *highlighted += QByteArray(readBuffer.size(), NORMAL);
            pos += readBuffer.size();
        }
    }
    _ioDevice->close();
    return buffer;
}

bool Chunks::write(QIODevice &iODevice, qint64 pos, qint64 count)
{
    if (count == -1)
        count = _size;
    bool ok = iODevice.open(QIODevice::WriteOnly);
    if (ok)
    {
        for (qint64 idx=pos; idx < count; idx += BUFFER_SIZE)
        {
            QByteArray ba = data(idx, BUFFER_SIZE);
            iODevice.write(ba);
        }
        iODevice.close();
    }
    return ok;
}


// ***************************************** Set and get highlighting infos

void Chunks::setDataChanged(qint64 pos, bool dataChanged)
{
    if ((pos < 0) || (pos >= _size))
        return;
    int chunkIdx = getChunkIndex(pos);
    qint64 posInBa = pos - _chunks[chunkIdx].absPos;
    _chunks[chunkIdx].dataChanged[(int)posInBa] = char(dataChanged);
}

bool Chunks::dataChanged(qint64 pos)
{
    QByteArray highlighted;
    data(pos, 1, &highlighted);
    return bool(highlighted.at(0));
}


// ***************************************** Search API

qint64 Chunks::indexOf(const QByteArray &ba, qint64 from)
{
    qint64 result = -1;
    QByteArray buffer;

    for (qint64 pos = from; (pos < _size) && (result < 0); pos += BUFFER_SIZE)
    {
        buffer = data(pos, BUFFER_SIZE + ba.size() - 1);

        int findPos = buffer.indexOf(ba);

        if (findPos >= 0)
            result = pos + (qint64)findPos;
    }

    return result;
}

qint64 Chunks::lastIndexOf(const QByteArray &ba, qint64 from)
{
    qint64 result = -1;
    QByteArray buffer;

    for (qint64 pos = from; (pos > 0) && (result < 0); pos -= BUFFER_SIZE)
    {
        qint64 sPos = pos - BUFFER_SIZE - (qint64)ba.size() + 1;

        if (sPos < 0)
            sPos = 0;

        buffer = data(sPos, pos - sPos);

        int findPos = buffer.lastIndexOf(ba);

        if (findPos >= 0)
            result = sPos + (qint64)findPos;
    }
    return result;
}


// ***************************************** Char manipulations

bool Chunks::insert(qint64 pos, char b)
{
    if ((pos < 0) || (pos > _size))
        return false;
    int chunkIdx;
    if (pos == _size)
        chunkIdx = getChunkIndex(pos-1);
    else
        chunkIdx = getChunkIndex(pos);
    qint64 posInBa = pos - _chunks[chunkIdx].absPos;
    _chunks[chunkIdx].data.insert(posInBa, b);
    _chunks[chunkIdx].dataChanged.insert(posInBa, char(1));
    _chunks[chunkIdx].originalData.insert(posInBa, char(0));
    _chunks[chunkIdx].hasOriginal.insert(posInBa, char(0));
    for (int idx=chunkIdx+1; idx < _chunks.size(); idx++)
        _chunks[idx].absPos += 1;
    _size += 1;
    _pos = pos;
    return true;
}

bool Chunks::overwrite(qint64 pos, char b)
{
    if ((pos < 0) || (pos >= _size))
        return false;

    int chunkIdx = getChunkIndex(pos);
    qint64 posInBa = pos - _chunks[chunkIdx].absPos;
    _chunks[chunkIdx].data[(int)posInBa] = b;
    // Only highlight if byte actually differs from original disk data;
    // inserted bytes (no original) are always highlighted.
    if (_chunks[chunkIdx].hasOriginal[(int)posInBa])
        _chunks[chunkIdx].dataChanged[(int)posInBa] = (b != _chunks[chunkIdx].originalData[(int)posInBa]) ? char(1) : char(0);
    else
        _chunks[chunkIdx].dataChanged[(int)posInBa] = char(1);
    _pos = pos;

    return true;
}

bool Chunks::removeAt(qint64 pos)
{
    if ((pos < 0) || (pos >= _size))
        return false;

    int chunkIdx = getChunkIndex(pos);

    qint64 posInBa = pos - _chunks[chunkIdx].absPos;
    _chunks[chunkIdx].data.remove(posInBa, 1);
    _chunks[chunkIdx].dataChanged.remove(posInBa, 1);
    _chunks[chunkIdx].originalData.remove(posInBa, 1);
    _chunks[chunkIdx].hasOriginal.remove(posInBa, 1);

    for (int idx=chunkIdx+1; idx < _chunks.size(); idx++)
        _chunks[idx].absPos -= 1;

    _size -= 1;
    _pos = pos;

    return true;
}


// ***************************************** Utility functions

char Chunks::operator[](qint64 pos)
{
    return data(pos, 1).at(0);
}

qint64 Chunks::pos()
{
    return _pos;
}

qint64 Chunks::size()
{
    return _size;
}

int Chunks::getChunkIndex(qint64 absPos)
{
    // Fast path: binary search for an existing chunk that contains absPos.
    int foundIdx = findChunkIdx(absPos);
    if (foundIdx >= 0)
        return foundIdx;

    // Chunk not found — we need to load it from the IODevice.
    // Walk linearly to compute insertIdx and ioDelta.
    int insertIdx = 0;
    qint64 ioDelta = 0;

    for (int idx = 0; idx < _chunks.size(); idx++)
    {
        const Chunk &chunk = _chunks[idx];
        if (absPos < chunk.absPos)
        {
            insertIdx = idx;
            break;
        }
        ioDelta += chunk.data.size() - CHUNK_SIZE;
        insertIdx = idx + 1;
    }

    Chunk newChunk;
    qint64 readAbsPos = absPos - ioDelta;
    qint64 readPos = (readAbsPos & READ_CHUNK_MASK);
    _ioDevice->open(QIODevice::ReadOnly);
    _ioDevice->seek(readPos);
    newChunk.data = _ioDevice->read(CHUNK_SIZE);
    _ioDevice->close();
    newChunk.absPos = absPos - (readAbsPos - readPos);
    newChunk.originalData = newChunk.data;
    newChunk.dataChanged = QByteArray(newChunk.data.size(), char(0));
    newChunk.hasOriginal = QByteArray(newChunk.data.size(), char(1));
    _chunks.insert(insertIdx, newChunk);

    evictCleanChunks();

    return insertIdx;
}

int Chunks::findChunkIdx(qint64 absPos) const
{
    // Binary search: _chunks is sorted by absPos.
    int lo = 0, hi = _chunks.size() - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        const Chunk &c = _chunks[mid];
        if (absPos < c.absPos)
            hi = mid - 1;
        else if (absPos >= c.absPos + c.data.size())
            lo = mid + 1;
        else
            return mid; // found: absPos is within this chunk
    }
    return -1;
}

void Chunks::evictCleanChunks()
{
    // Remove unmodified chunks when we exceed the limit.
    // Evict from the front (oldest loaded) to keep recently accessed chunks.
    if (_chunks.size() <= MAX_CACHED_CHUNKS)
        return;

    int toEvict = _chunks.size() - MAX_CACHED_CHUNKS;
    int idx = 0;
    while (toEvict > 0 && idx < _chunks.size())
    {
        // Only evict chunks with no modifications and standard size (not inserted/deleted)
        const Chunk &c = _chunks[idx];
        if (c.data.size() == CHUNK_SIZE
            && c.dataChanged == QByteArray(CHUNK_SIZE, char(0)))
        {
            _chunks.removeAt(idx);
            --toEvict;
        }
        else
        {
            ++idx;
        }
    }
}


#ifdef MODUL_TEST
int Chunks::chunkSize()
{
    return _chunks.size();
}

#endif
