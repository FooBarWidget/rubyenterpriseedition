#!/usr/bin/env ruby
5.times do
	GC.disable
	x = (1..1000000).map { "x" * 10 }
	GC.enable
	GC.start
end
