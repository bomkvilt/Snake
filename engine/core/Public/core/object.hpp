#ifndef CORE_OBJECT_HPP
#define CORE_OBJECT_HPP

#include "reflect/archived.hpp"
#include "core/core_types.hpp"
#include "interface/ilifecycle.hpp"

/**/ class Actor;


class Object : public reflect::FArchived
	, public ILifecycle
{
public:
	ARCH_BEGIN(reflect::FArchived)
		ARCH_END()
public:
	Object();

	FName  GetName() const;		//!< returns object's name
	UID	   GetUID()  const;		//!< returns object's uid
	Actor* GetActor() const;	//!< returns object's actor

private:
	Actor* actor;	//!< assigned actor
	FName  name;	//!< object name
	UID	   uid;		//!< unique object id
};


#endif //!CORE_OBJECT_HPP