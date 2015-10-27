//
// OpenThread library, Copyright (C) 2002 - 2015  The Open Thread Group
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include <OpenThreads/ThreadPool>
#include <OpenThreads/ScopedLock>
#include <memory>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <assert.h>

#ifdef _WIN32
#include <conio.h>
#endif

struct Task
{
	int num;
};

class Worker : public OpenThreads::WorkerThread
{
public:
	void executeTask(void* t)
	{
		Task* task = static_cast<Task*>(t);
		std::cout << "Runing task " << task->num << std::endl;
		OpenThreads::Thread::microSleep(1000000);
		std::cout << "End of task " << task->num << std::endl;
		OpenThreads::ScopedLock<OpenThreads::Mutex> slock(_s_mutex);
		++_s_numTasksDone;
	}
	static int numTasksDone() 
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> slock(_s_mutex);
		return _s_numTasksDone;
	}
private:
	static int _s_numTasksDone;
	static OpenThreads::Mutex _s_mutex;
};
int Worker::_s_numTasksDone(0);
OpenThreads::Mutex Worker::_s_mutex;
typedef std::unique_ptr<Worker> WorkerPtr;
typedef std::vector<WorkerPtr> Workers;

class Manager : public OpenThreads::WorkerThread
{
public:
	Manager(Workers& workers) : _workers(workers), _i(0)
	{
	}

	void init()
	{
		_tasks.resize(10);
		for (Tasks::iterator it = _tasks.begin(); it != _tasks.end(); ++it)
			it->num = std::distance(_tasks.begin(), it);
		std::cout << "Created " << _tasks.size() << " tasks" << std::endl;
	}

	void executeTask(void* t)
	{
		if (_i >= _tasks.size())
			return;

		size_t num = std::min(_tasks.size() - _i, (size_t)5);

		std::cout << "Manager sends " << num << " tasks to workers ";
		for (size_t i = 0; i < num; ++i)
		{
			if (i > 0)
				std::cout << ", ";
			size_t tidx = _i + i;
			assert(tidx < _tasks.size());
			size_t widx = tidx % _workers.size();
			std::cout << widx;
			_workers[widx]->queue(&_tasks[tidx]);
		}
		std::cout << std::endl;
		_i += num;

		if (_i >= _tasks.size())
		{
			std::cout << "Manager's job done" << std::endl;
			stop(false);
		}
	}

private:
	Workers& _workers;
	size_t _i;
	typedef std::vector<Task> Tasks;
	Tasks _tasks;
};
typedef std::unique_ptr<Manager> ManagerPtr;

int main(int argc, char **argv) 
{
	Workers workers;

	for (int i = 0; i < OpenThreads::GetNumberOfProcessors(); ++i)
		workers.push_back(WorkerPtr(new Worker));


	OpenThreads::ThreadPool pool;

	// Let the workers arrive to work
	for (Workers::iterator it = workers.begin(); it != workers.end(); ++it)
		pool.add(it->get());
	std::cout << "Spawned " << workers.size() << " threads." << std::endl;

	// Manager always arrives late
	OpenThreads::Thread::microSleep(100000);
	ManagerPtr manager(new Manager(workers));
	pool.add(manager.get());

	OpenThreads::Thread::microSleep(100000);

	while (manager->isRunning())
	{
		manager->queue(&pool);
		OpenThreads::Thread::microSleep(50000);
	}
	
	std::cout << "Press any key to terminate" << std::endl;

#ifdef _WIN32
	_getch();
#else
	std::cin.get();
#endif

	std::cout << "Stopping all threads ..." << std::endl;
	pool.stop();
	std::cout << "Done." << std::endl;

	std::cout << "Ran " << Worker::numTasksDone() << " tasks" << std::endl;
}
