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


TaskContext::TaskContext()
	: _pool(nullptr), _worker(nullptr)
{
}

TaskContext::TaskContext(ThreadPool* pool, WorkerThread* worker)
	: _pool(pool), _worker(worker)
{
	assert(pool);
	assert(worker);
}

bool TaskContext::shouldStop(bool isSafeCancelPoint)
{
	assert(_worker);
	bool ret = (_worker->_flags & WorkerThread::STOPPING) == WorkerThread::STOPPING;
	if (ret && isSafeCancelPoint)
		_worker->testCancel();
	return ret;
}

Task::Task()
{
}

Task::~Task()
{
}



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
	_context = TaskContext(pool, this);
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

void WorkerThread::executeTask(Task* task)
{
	task->execute(_context);
}

void WorkerThread::queue(Task* task)
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



ThreadPool::ThreadPool(DispatchOp* defaultDispatch)
	: _stopping(false), _defaultDispatch(defaultDispatch)
{
	if (!_defaultDispatch)
		_defaultDispatch = std::unique_ptr<DispatchOp>(new DispatchDummy);
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

int ThreadPool::stop(bool finishAllTasks, unsigned int politeTimeout, unsigned int overallTimeout, bool fatality)
{
	if (overallTimeout < politeTimeout)
		overallTimeout = politeTimeout;

	Workers all, alive;
	{
		ScopedLock<Mutex> slock(_mutex);
		_stopping = true;
		alive.swap(_workers);
		all = alive;
	}

#define GOTO_END(m) { method = m; break; }

	int method = -1;
	while (1)
	{
		if (alive.empty())
			GOTO_END(1);

		if (politeTimeout > 0)
		{
			for (Workers::iterator it = alive.begin(); it != alive.end(); ++it)
			{
				//std::cout << "asking " << it->first << " to stop" << std::endl;
				it->second->stop(finishAllTasks);
			}
			waitForTermination(alive, politeTimeout);
		}

		if (alive.empty())
			GOTO_END(1);

		unsigned int aggressiveTimeout = overallTimeout - politeTimeout;
		if (aggressiveTimeout > 0)
		{
			for (Workers::iterator it = alive.begin(); it != alive.end(); ++it)
			{
				//std::cout << "cancelling " << it->first << std::endl;
				it->second->cancel();
			}

			waitForTermination(alive, aggressiveTimeout);
		}

		if (alive.empty())
			GOTO_END(2);

		bool usedMethod3 = false;
		for (Workers::iterator it = alive.begin(); it != alive.end(); ++it)
		{
			if (fatality)
			{
				//std::cout << "killing thread " << it->first << std::endl;
				it->second->setCancelModeAsynchronous();
				it->second->cancel();
				usedMethod3 = true;
			}
			else
			{
				//std::cout << "thead " << it->first << " still alive after stop()" << std::endl;
				Workers::iterator skipJoin = all.find(it->first);
				if (skipJoin != all.end())
					all.erase(skipJoin);
			}
		}
		GOTO_END(usedMethod3 ? 3 : 0)
	}
	assert(method >= 0);

	// Join all threads (except those that we could not kill anyway)
	for (Workers::iterator it = all.begin(); it != all.end(); ++it)
		it->second->join();

	if (method == 0)
	{
		ScopedLock<Mutex> slock(_mutex);
		_stopping = false;
		_workers = alive;
	}

	return method;
}

void ThreadPool::workerEnded(WorkerThread* worker)
{
	ScopedLock<Mutex> slock(_mutex);

	int key = worker->getThreadId();
	assert(key > 0);

	Workers::iterator it = _workers.find(key);
	assert(it == _workers.end() || it->second == worker);
	if (it != _workers.end())
		_workers.erase(it);
	//std::cout << "Worker " << key << " ended.";
}

void ThreadPool::submit(Task* task, DispatchOp* op)
{
	ScopedLock<Mutex> slock(_mutex);
	if (op == nullptr) _defaultDispatch->dispatch(_workers, task);
	else op->dispatch(_workers, task);
}

ThreadPool::DispatchRoundRobin::DispatchRoundRobin()
	: _lastSize(0), _lastHash(0)
{
}

bool ThreadPool::DispatchRoundRobin::dispatch(const Workers& workers, Task* task)
{
	update(workers);
	if (_it == workers.end())
		return false;

	_it->second->queue(task);
	++_it;
	return true;
}

unsigned int ThreadPool::DispatchRoundRobin::hash(const Workers& workers)
{
	unsigned int sum = 0;
	for (Workers::const_iterator it = workers.begin(); it != workers.end(); ++it)
		sum ^= (unsigned int)it->first;
	return sum;
}

void ThreadPool::DispatchRoundRobin::update(const Workers& workers)
{
	unsigned int curHash = hash(workers);
	if (_lastSize == 0 || _lastSize != workers.size() || _lastHash == 0 || _lastHash != curHash || _it == workers.end())
	{
		_lastHash = curHash;
		_lastSize = workers.size();
		_it = workers.begin();
	}
}
