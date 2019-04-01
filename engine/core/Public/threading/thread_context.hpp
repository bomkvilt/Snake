#ifndef THREAD_CONTEXT_HPP
#define THREAD_CONTEXT_HPP

#include <stack>
#include <boost/noncopyable.hpp>
#include "config/engine_config.hpp"


struct IInitialiser 
{};


class ThreadContext final : public boost::noncopyable
{
public:
	struct ScopeInitHelper final
	{	//!^ RAII initialisation wrapper
		ScopeInitHelper(IInitialiser& init)
		{ 
			ThreadContext::PushInitialiser(init);
		}
		~ScopeInitHelper() 
		{ 
			ThreadContext::PopInitialiser(); 
		}
	};

public: //~~~~~~~~~~~~~~| world
	static void SetWorld(class World* newWorld);
	static class World* GetWorld();

public: //~~~~~~~~~~~~~| config
	static FEngineConfig& GetConfig();
	static void SetConfig(FEngineConfig newConfig);

public: //~~~~~~~~~~~~~~| Initalisers
	static void PushInitialiser(IInitialiser& init);
	static void PopInitialiser();

	template<typename T>
	static T& TopInitialiser()
	{
		if (auto& inits = Get().inits; inits.size())
		{
			return dynamic_cast<T*>(inits.top());
		}
	}

private: //~~~~~~~~~~~~~~| internal
	static ThreadContext& Get();

private:
	// stack of initialisers
	std::stack<IInitialiser*> inits;

	// current world
	class World* world;

	// engine config
	static FEngineConfig config;
};


#endif //!THREAD_CONTEXT_HPP