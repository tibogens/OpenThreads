/* -*-c++-*- OpenThreads library, Copyright (C) 2002 - 2007  The Open Thread Group
 *
 * This library is open source and may be redistributed and/or modified under  
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or 
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/

#include <OpenThreads/ThreadPool>
#include <OpenThreads/ScopedLock>
#include <algorithm>
#include <assert.h>
//#include <iostream>
using namespace OpenThreads;


WorkerThread::WorkerThread()
	: Thread(), _pool(nullptr), _flags(0)
{

}

WorkerThread::~WorkerThread()
{

}

void WorkerThread::setPool(ThreadPool* pool)
{
	assert(!isRunning());
	assert(!_pool);
	assert(pool);
	_pool = pool;
}

void WorkerThread::run()
{
	assert(_pool);

	class RunEndSignal
	{
	public:
		RunEndSignal(ThreadPool* tp, WorkerThread* w) : tp(tp), w(w) {}
		~RunEndSignal() { tp->workerEnded(w); }
		ThreadPool* tp; WorkerThread* w;
	} endSignal(_pool, this);

	init();

	{
		ScopedLock<Mutex> slock(_mutex);
		while (!shouldStop())
		{
			while (_tasks.empty())
				_condition.wait(&_mutex);

			if (shouldStop())
				break;

			if (_tasks.size() == 1 && *(_tasks.begin()) == nullptr) 
			{
				// Take a shortcut if we're only performing a no-op
				_tasks.clear();
			}
			else
			{
				Tasks copy = _tasks;
				_tasks.clear();

				{
					ReverseScopedLock<Mutex> sunlock(_mutex);
					for (Tasks::iterator it = copy.begin(); it != copy.end(); ++it)
					{
						if (*it != nullptr)
							executeTask(*it);
					}
				}
			}
		}
	}
}

void WorkerThread::queue(void* task)
{
	ScopedLock<Mutex> slock(_mutex);
	_tasks.push_back(task);
	//std::cout << "queued " << _tasks.size() << "th task" << std::endl;
	_condition.signal();
}

void WorkerThread::stop(bool finishTasks)
{
	ScopedLock<Mutex> slock(_mutex);
	_flags |= STOPPING | (finishTasks ? STOP_AFTER_TASKS : 0);
	_tasks.push_back(nullptr);
	_condition.signal();
}

bool WorkerThread::shouldStop()
{
	if ((_flags & STOPPING) == STOPPING)
	{
		if ((_flags & STOP_AFTER_TASKS) == STOP_AFTER_TASKS)
		{
			// Stop when queue is empty
			if (_tasks.empty())
				return true;
		}
		else
			return true; // Stop now
	}

	// Note: this throws if the cancel event is set. Therefore shouldStop()
	// must be called with this in mind - in particular, don't hold a mutex
	// without a proper RAII unlocking scheme.
	// This can be annoying if we have to use thread killing after all timeouts.
	int ret = testCancel();
	assert(ret == 0);

	return false;
}



ThreadPool::ThreadPool()
	: _stopping(false)
{

}

ThreadPool::~ThreadPool()
{

}

int ThreadPool::add(WorkerThread* worker)
{
	assert(!worker->isRunning());
	if (worker->isRunning())
		return 0;

	{
		ScopedLock<Mutex> slock(_mutex);
		if (_stopping)
			return 0;
	}

	worker->setPool(this);
	worker->start();

	int key = worker->getThreadId();
	_workers[key] = worker;
	return key;
}

void ThreadPool::waitForTermination(Workers& workers, unsigned int timeout)
{
	unsigned int startWait = Thread::getTickCount();
	while (!workers.empty() && (Thread::getTickCount() - startWait) < timeout)
	{
		bool idledThisLoop = true;
		for (Workers::iterator it = workers.begin(); it != workers.end(); ++it)
		{
			if (!it->second->isRunning())
			{
				workers.erase(it);
				idledThisLoop = false;
				break;
			}
		}

		if (idledThisLoop)
			Thread::microSleep(50000);
	}
}

void ThreadPool::stop()
{
	Workers all, alive;
	
	{
		ScopedLock<Mutex> slock(_mutex);
		_stopping = true;
		alive.swap(_workers);
		all = alive;
		for (Workers::iterator it = alive.begin(); it != alive.end(); ++it)
		{
			//std::cout << "asking " << it->first << " to stop" << std::endl;
			it->second->stop(true);
		}
	}

	waitForTermination(alive, 3000);

	for (Workers::iterator it = alive.begin(); it != alive.end(); ++it)
	{
		//std::cout << "cancelling " << it->first << std::endl;
		it->second->cancel();
	}

	waitForTermination(alive, 30000);

	for (Workers::iterator it = alive.begin(); it != alive.end(); ++it)
	{
		//std::cout << "killing " << it->first << std::endl;
		it->second->setCancelModeAsynchronous();
		it->second->cancel();
	}

	for (Workers::iterator it = all.begin(); it != all.end(); ++it)
		it->second->join();
}

void ThreadPool::workerEnded(WorkerThread* worker)
{
	ScopedLock<Mutex> slock(_mutex);

	int key = worker->getThreadId();
	assert(key > 0);

	Workers::iterator it = _workers.find(key);
	assert(it == _workers.end() || it->second == worker);
	//if (it != _workers.end())
	//	_workers.erase(it);
	//std::cout << "Worker " << key << " ended.";
}
