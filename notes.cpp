/*
utils (std, alogo, templates, containers, ranges, etc..) / platform
core (object system, gc, threads/tasks/fibers, reflection, serialization, modules, config, log, stats)
primitives (common structures, interfaces and data formats, independent from systems)
engine/systems
framework
game/server
editor 
*/

Task<> SampleCode(Handle<ResType> handle1, Handle<ResType> handle2)
{
	SyncResources sync;
	
	FWriteOnScope<ResType> res1(sync, handle1, BlockWrite); // BlockWrite, BlockWriteRead, NoBlock
	{
		FReadOnScope<ResType> res2(sync, handle2, BlockWrite); // BlockWrite, NoBlock
		sync.sync();
		res1->InteractWith(res2);
	}
}

