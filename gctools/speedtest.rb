#!/usr/bin/env ruby
if GC.respond_to?(:copy_on_write_friendly=)
	GC.copy_on_write_friendly = true
end
5.times do
	GC.disable if GC.respond_to?(:disable)
	x = (1..1000000).map { "x" * 10 }
	GC.enable if GC.respond_to?(:enable)
	GC.start
end
