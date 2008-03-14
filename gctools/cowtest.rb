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
	GC.copy_on_write_friendly = true
	load_rails
	ObjectSpace.garbage_collect
	
	pid = fork do
		pid = fork do
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