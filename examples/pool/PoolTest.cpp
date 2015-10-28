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

class Task : public OpenThreads::Task
{
protected:
	void execute(OpenThreads::TaskContext& ctxt)
	{
		std::cout << "Running task " << num << std::endl;
		int numSteps = 10, executed = 0;
		unsigned int elapsed = 0, timeout = 5000000;
		unsigned int timeoutStep = timeout / numSteps;
		for (int i=0;i<numSteps;++i)
		{
			OpenThreads::Thread::microSleep(timeoutStep);
			++executed;
			if (ctxt.shouldStop())
				break;
		}
		
		std::cout << "End of task" << num;
		if (executed == numSteps)
			std::cout << ", ran all steps";
		else
			std::cout << ", ran " << executed << "/" << numSteps << " steps";
		std::cout << std::endl;

		//if (num == 0)
		//	ctxt.getWorker()->queue(this);

		OpenThreads::ScopedLock<OpenThreads::Mutex> slock(_s_mutex);
		_s_done.push_back(num);
	}
public:
	int num;
	static int numDone()
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> slock(_s_mutex);
		return _s_done.size();
	}
private:
	static std::list<int> _s_done;
	static OpenThreads::Mutex _s_mutex;
};
std::list<int> Task::_s_done;
OpenThreads::Mutex Task::_s_mutex;
typedef std::vector<Task> Tasks;


typedef std::unique_ptr<OpenThreads::WorkerThread> WorkerPtr;
typedef std::vector<WorkerPtr> Workers;

void createTasks(Tasks& tasks, int num = 10)
{
	tasks.resize(num);
	for (int i = 0; i < num; ++i)
		tasks[i].num = i;
}

void submit(Tasks& tasks, OpenThreads::ThreadPool& pool, unsigned int period = 0)
{
	std::list<Task*> pending;
	for (Tasks::iterator it = tasks.begin(); it != tasks.end(); ++it)
		pending.push_back(&*it);

	std::cout << "Created " << tasks.size() << " tasks" << std::endl;

	while (!pending.empty())
	{
		pool.submit(pending.front());
		pending.pop_front();
		OpenThreads::Thread::microSleep(period*1000);
	}

	std::cout << "Dispatched all tasks for this sample" << std::endl;
}

int main(int argc, char **argv) 
{
	// Prepare the thread pool
	
	// Workers first (we own them)
	Workers workers;
	for (int i = 0; i < OpenThreads::GetNumberOfProcessors(); ++i)
		workers.push_back(WorkerPtr(new OpenThreads::WorkerThread));
	
	// Then the pool, with an appropriate dispatcher
	OpenThreads::ThreadPool pool(new OpenThreads::ThreadPool::DispatchRoundRobin);
	// Each call to add() starts the worker.
	for (Workers::iterator it = workers.begin(); it != workers.end(); ++it)
		pool.add(it->get());
	
	std::cout << "Spawned " << workers.size() << " threads." << std::endl;

	Tasks tasks;
	createTasks(tasks);
	submit(tasks, pool);

	std::cout << "Press any key to terminate" << std::endl;

#ifdef _WIN32
	_getch();
#else
	std::cin.get();
#endif

	std::cout << "Stopping all threads ..." << std::endl;
	int stopRet = pool.stop();

	if (stopRet == 0)
	{
		std::cout << "WARNING: killing remaining threads ..." << std::endl;
		stopRet = pool.stop(false, 0, 1000, true);
		std::cout << "Done (return code " << stopRet << ")." << std::endl;
		assert(pool.stop(false, 0, 0, false) == 1);
	}
	else
	{
		std::cout << "Done (return code " << stopRet << ")." << std::endl;
	}

	std::cout << "Ran " << Task::numDone() << " tasks" << std::endl;
}
