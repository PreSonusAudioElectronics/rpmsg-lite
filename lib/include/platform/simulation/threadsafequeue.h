
#ifndef _threadsafequeue_h
#define _threadsafequeue_h

#include <mutex>
#include <queue>
#include <cstring>
#include <memory>

// getSize

class ThreadsafeQueue 
{
public:

	ThreadsafeQueue(uint32_t nElements, size_t elementSize):
	elementSize(elementSize), nElements(nElements),
	mem(std::make_unique<char[]>(elementSize))
	{
		// mem = std::make_unique<char[]>(elementSize);
	}

	int put( void *msg )
	{
		std::lock_guard<std::mutex> lg(mutex);
		if( !isFull() )
		{
			memcpy( &mem + wrIdx*elementSize, msg, elementSize );
			wrIdx = incIdx(wrIdx);
			nStored++;
			return 0;
		}
		else
		{
			return -1;
		}
	}

	int get( void *msg )
	{
		if( nStored > 0 && (msg) )
		{
			std::lock_guard<std::mutex> lg(mutex);
			memcpy( msg, &mem + rdIdx*elementSize, elementSize);
			rdIdx = incIdx(rdIdx);
			nStored--;
			return 0;
		}
		else
		{
			return -1;
		}
	}

	uint32_t getSize()
	{
		return nStored;
	}

private:
	std::mutex mutex;
	size_t elementSize;
	uint32_t nElements;
	uint32_t nStored = 0;
	uint32_t wrIdx = 0;
	uint32_t rdIdx = 0;
	std::unique_ptr<char[]> mem;

	inline uint32_t incIdx(uint32_t idx)
	{
		uint32_t retval = idx++;
		if ( retval >= nElements )
		{
			retval = 0;
		}
		return retval;
	}

	inline bool isFull()
	{
		if( nStored > 0 && wrIdx==rdIdx )
		{
			return true;
		}
		else
			return false;
	}

};


#endif // _threadsafequeue_h
