#include <iostream>
#include <queue>

#include "core/world.hpp"
#include "core/object.hpp"
#include "threading/thread_pull.hpp"



namespace WorldUtiles
{
	using SStageTickTable = std::array<std::array<bool, ETickPhaseCount>, ELifeStageCount>;

	constexpr SStageTickTable GetStageTickTable()
	{
		auto table = SStageTickTable{ {0} };
		auto allow = [&](ELifeStage stage, ETickPhase phase)
		{
			table[UInt8(stage)][UInt8(phase)] = true;
		};
		auto allowAll = [&](ELifeStage stage)
		{
			for (UInt8 i = 0; i < ETickPhaseCount; ++i)
			{
				allow(stage, ETickPhase(i));
			}
		};
		allowAll(ELifeStage::eActive);
		allow(ELifeStage::ePaused, ETickPhase::eInputParsing);
		allow(ELifeStage::ePaused, ETickPhase::eSerialisation);
		allow(ELifeStage::ePaused, ETickPhase::ePreRender);
		allow(ELifeStage::ePaused, ETickPhase::eInRender);
		return table;
	}
}



UNIQUE(World) World::New()
{
	return std::make_unique<World>();
}

World::World()
	: worldContext(std::make_unique<ThreadContext::WorldContext>())
	, lastUID(0)
{
	worldContext->world = this;
	ticks.AddEvent(ETickPhase::eInputParsing, [this](ETickPhase)
	{
		std::scoped_lock _(nonstartedObjects_mu);
		for (auto* object : nonStartedObjects)
		{
			object->OnConstructed();
			object->OnBeginPlay();
		}
	});
}

World::~World()
{}


void World::OnConstructed()
{
	ILifecycle::OnConstructed();
}

void World::OnBeginPlay()
{
	ILifecycle::OnBeginPlay();

	time_current = SClock::now();
	StartTicks();
}

void World::OnPause()
{
	ILifecycle::OnPause();
}

void World::OnResume()
{
	ILifecycle::OnResume();

	StartTicks();
}

void World::OnEndPlay()
{
	ILifecycle::OnEndPlay();
}

void World::OnDestruction()
{
	ILifecycle::OnDestruction();

	while (ntasks)
	{
		std::this_thread::yield();
	}
}


void World::AddTask(threading::FTask::ptr&& task)
{
	using namespace threading;

	task->SetOnBegin([this]()
	{	//TODO: add a multibinding
		ThreadContext::SetWorldContext(worldContext.get());
	});
	task->SetOnDone([this]()
	{	//TODO: add a multibinding
		--ntasks;
	});
	IThreadpool::Get().AddTask(std::move(task));
	++ntasks;
}


TickManager& World::GetTickManager()
{
	return ticks;
}

const TickManager& World::GetTickManager() const
{
	return ticks;
}


void World::StartTicks()
{
	using namespace threading;

	if (ntasks > 1)
	{
		return;
	}
	AddTask(FLambdaTask::New([this]() { DoTick(); }));
}

void World::DoTick()
{
	if (GetLifeStage() != ELifeStage::eActive)
	{
		return;
	}
	Sleep();
	NextLoop();

	std::cout << time_lastStep << std::endl;
}

void World::Sleep()
{
	using namespace std::chrono_literals;
	using namespace std::chrono;

	auto time_step = duration_cast<nanoseconds>(1s) / config.fps;
	auto time_before = time_current;
	{	/// time passed since a last call
		auto time_now = SClock::now();
		auto time_spent = time_now - time_before;
		/// sleep if required
		if (time_spent < time_step)
		{
			std::this_thread::sleep_until(time_before + time_step);
		}
	}
	{	/// update a current time
		time_current = SClock::now();
		time_lastStep = duration_cast<microseconds>(time_current - time_before).count() / 1e6f;
		/// update simulation time
		time_fromStart += time_lastStep;
	}
}

void World::NextLoop()
{
	using namespace threading;
	using namespace WorldUtiles;

	constexpr auto tickStages = GetStageTickTable();
	constexpr auto tickPhases = ETickPhaseList();

	/// create and link tasks
	std::vector<FTask::ptr> tasks;
	for (auto phase : tickPhases)
	{
		if (!tickStages[(UInt8)GetLifeStage()][(UInt8)phase])
		{
			continue;
		}

		auto [publicTasks, privateTasks] = ticks.GetTasks(phase, time_lastStep);
		if (tasks.size())
		{
			tasks.back()->Next(publicTasks);
		}
		publicTasks->Next(privateTasks);
		tasks.emplace_back(std::move(publicTasks));
		tasks.emplace_back(std::move(privateTasks));
	}
	/// next cirule
	{
		auto tickTask = FLambdaTask::New([this]() { DoTick(); });
		if (tasks.size())
		{
			tasks.back()->Next(tickTask);
		}
		tasks.emplace_back(std::move(tickTask));
	}
	/// push tasks
	for (auto& task : tasks)
	{
		AddTask(std::move(task));
	}
}

void World::RegisterObject(UNIQUE(Object)&& object)
{
	auto* ptr = object.get();
	{
		std::scoped_lock _(createdObjects_mu, assignedObjects_mu);

		createdObjects[ptr] = std::move(object);

		if (auto owner = ptr->GetOwner())
		{
			assignedObjects[owner].push_back(ptr);
		}
	}
	{
		std::scoped_lock _(nonstartedObjects_mu);
		nonStartedObjects.emplace_back(ptr);
	}
}

void World::RemoveObjects()
{
	std::scoped_lock _(removedObjects_mu, createdObjects_mu);

	auto pos = removedObjectsList.rbegin();
	auto end = removedObjectsList.rend();
	for (; pos != end; ++pos)
	{
		auto* object = *pos;
		object->OnDestruction();
		createdObjects.erase(object);
	}
	removedObjects.clear();
	removedObjectsList.clear();
}

void World::DelObject(Object* object)
{
	struct FNode : public boost::noncopyable
	{
		Object* owner;
		std::vector<Object*>::const_iterator pos;
		std::vector<Object*>::const_iterator end;

		FNode(Object* object, World* world)
			: owner(object)
		{
			auto& vector = world->assignedObjects.find(object)->second;
			pos = vector.begin();
			end = vector.end();
		}
	};

	std::scoped_lock _(removedObjects_mu, assignedObjects_mu);

	auto nodes = std::stack<FNode>();
	nodes.emplace(object, this);
	
	while (nodes.size() > 0)
	{
		auto& top = nodes.top();
		if (top.pos == top.end)
		{
			top.owner->OnEndPlay();
			removedObjects.insert(top.owner);
			removedObjectsList.push_back(top.owner);
			assignedObjects.erase(top.owner);
			continue;
		}
		nodes.emplace(*(++top.pos), this);
	}
}

bool World::IsValid(Object* object)
{
	std::shared_lock _(removedObjects_mu);
	auto pos = removedObjects.find(object);
	auto end = removedObjects.end();
	return pos == end;
}
