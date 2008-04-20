#!/usr/bin/env ruby
# Script for checking whether pages are made dirty after a garbage collection.

def load_rails
	require 'rubygems'
	gem 'rails'
	require 'initializer'
	require 'active_support'
	require 'action_controller'
	require 'action_view'
	require 'active_record'
	puts "Loaded Ruby on Rails version #{Rails::VERSION::STRING}"
end

def print_statistics
	if ObjectSpace.respond_to?(:statistics)
		puts "========================================================="
		puts "--------------- Before garbage collection ---------------"
		puts ObjectSpace.statistics
	end
	ObjectSpace.garbage_collect
	if ObjectSpace.respond_to?(:statistics)
		puts "--------------- After garbage collection ----------------"
		puts ObjectSpace.statistics
		puts "========================================================="
	end
end

def start
	if GC.respond_to?(:copy_on_write_friendly=)
		GC.copy_on_write_friendly = true
	end
	load_rails
	ObjectSpace.garbage_collect
	
	pid = fork do
		pid = fork do
			if GC.respond_to?(:initialize_debugging)
				ENV['RD_PROMPT_BEFORE_GC'] = '1'
				ENV['RD_PROMPT_BEFORE_SWEEP'] = '1'
				ENV['RD_PRINT_SWEEPED_OBJECTS'] = '1'
				GC.initialize_debugging
			end
			
			puts "Child process #{$$} created. Press Enter to start garbage collection."
			STDIN.readline
			print_statistics
			puts "Garbage collection finished. Press Enter to exit."
			STDIN.readline
			exit!
		end
		Process.waitpid(pid)
		exit!
	end
	Process.waitpid(pid)
end

start