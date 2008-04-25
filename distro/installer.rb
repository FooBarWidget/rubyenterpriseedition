#!/usr/bin/env ruby

class Installer
	ROOT = File.expand_path(File.dirname(__FILE__))
	PASSENGER_WEBSITE = "http://www.modrails.com"
	EMM_RUBY_WEBSITE = "http://www.rubyenterpriseedition.com"
	
	def start
		Dir.chdir(ROOT)
		@version = File.read("version.txt")
		show_welcome_screen
		check_dependencies
		ask_installation_prefix
		
		steps = [
		  :configure_tcmalloc, :compile_tcmalloc, :install_tcmalloc,
		  :configure_ruby,     :compile_ruby,     :install_ruby,
		  :install_rubygems,
		  :install_useful_libraries
		]
		steps.each do |step|
			if !self.send(step)
				exit 1
			end
		end
		show_finalization_screen
	ensure
		reset_terminal_colors
	end

private
	def show_welcome_screen
		color_puts "<banner>Welcome to the Ruby Enterprise Edition installer</banner>"
		color_puts "This installer will help you install Ruby Enterprise Edition #{@version}."
		color_puts "Don't worry, none of your system files will be touched if you don't want them"
		color_puts "to, so there is no risk that things will screw up."
		puts
		color_puts "You can expect this from the installation process:"
		puts
		color_puts "  <b>1.</b> Ruby Enterprise Edition will be compiled and optimized for speed for this"
		color_puts "     system."
		color_puts "  <b>2.</b> Ruby on Rails will be installed for Ruby Enterprise Edition."
		color_puts "  <b>3.</b> You will learn how to tell Phusion Passenger to use Ruby Enterprise"
		color_puts "     Edition instead of regular Ruby."
		puts
		color_puts "<b>Press Enter to continue, or Ctrl-C to abort.</b>"
		wait
	end
	
	def check_dependencies
		# TODO: check for zlib-dev and openssl-dev. Ruby needs them for some modules.
	end
	
	def ask_installation_prefix
		line
		color_puts "<banner>Target directory</banner>"
		puts
		puts "Where would you like to install Ruby Enterprise Edition to?"
		puts "(All Ruby Enterprise Edition files will be put inside that directory.)"
		puts
		@old_prefix = File.read("source/.prefix.txt") rescue nil
		@prefix = query_directory(@old_prefix || "/opt/ruby-enterprise-#{@version}")
		@prefix_changed = @prefix != @old_prefix
		File.open("source/.prefix.txt", "w") do |f|
			f.write(@prefix)
		end
	end
	
	def configure_tcmalloc
		return configure_autoconf_package('source/vendor/google-perftools-0.97',
			'the memory allocator for Ruby Enterprise Edition',
			'--disable-dependency-tracking')
	end
	
	def compile_tcmalloc
		Dir.chdir('source/vendor/google-perftools-0.97') do
			return sh("make libtcmalloc_minimal.la")
		end
	end
	
	def install_tcmalloc
		return install_autoconf_package('source/vendor/google-perftools-0.97',
		  'the memory allocator for Ruby Enterprise Edition') do
			sh("mkdir", "-p", "#{@prefix}/lib") &&
			sh("cp .libs/libtcmalloc_minimal.so* '#{@prefix}/lib/'")
		end
	end
	
	def configure_ruby
		return configure_autoconf_package('source', 'Ruby Enterprise Edition')
	end
	
	def compile_ruby
		Dir.chdir("source") do
			# No idea why, but sometimes 'make' fails unless we do this.
			sh("mkdir -p .ext/common")
			
			return sh("make EXTLIBS='-Wl,-rpath,#{@prefix}/lib -L#{@prefix}/lib -ltcmalloc_minimal'")
		end
	end
	
	def install_ruby
		return install_autoconf_package('source', 'Ruby Enterprise Edition')
	end
	
	def install_rubygems
		Dir.chdir("rubygems") do
			line
			color_puts "<banner>Installing RubyGems...</banner>"
			return sh("#{@prefix}/bin/ruby", "setup.rb", "--no-ri", "--no-rdoc")
		end
	end
	
	def install_useful_libraries
		line
		color_puts "<banner>Installing useful libraries...</banner>"
		gem = "#{@prefix}/bin/ruby #{@prefix}/bin/gem"
		status = sh("#{gem} install --no-rdoc --no-ri --backtrace rails mongrel fastthread")
		
		# These gems may fail to install if MySQL and SQLite headers aren't installed.
		# But we continue anyway.
		sh("#{gem} install --no-rdoc --no-ri --backtrace mysql")
		sh("#{gem} install --no-rdoc --no-ri --backtrace sqlite3-ruby")
		sh("#{gem} install --no-rdoc --no-ri --backtrace postgres")
		
		return status
	end
	
	def show_finalization_screen
		line
		color_puts "<banner>Ruby Enterprise Edition is successfully installed!</banner>"
		color_puts "If you're using <yellow>Phusion Passenger (#{PASSENGER_WEBSITE})</yellow>,"
		color_puts "and you want it to use Ruby Enterprise Edition, then edit your Apache"
		color_puts "configuration file, and change the 'RailsRuby' option:"
		puts
		color_puts "  <b>RailsRuby #{@prefix}/bin/ruby</b>"
		puts
		color_puts "If you ever want to uninstall Ruby Enterprise Edition, simply remove this"
		color_puts "directory:"
		puts
		color_puts "  <b>#{@prefix}</b>"
		puts
		color_puts "If you have any questions, feel free to visit our website:"
		puts
		color_puts "  <b>#{EMM_RUBY_WEBSITE}</b>"
		puts
		color_puts "Ruby Enterprise Edition is a product of Phusion (<yellow>www.phusion.nl</yellow>)"
	end

private
	DEFAULT_TERMINAL_COLORS = "\e[0m"

	def color_puts(message)
		puts substitute_color_tags(message)
	end
	
	def color_print(message)
		print substitute_color_tags(message)
	end

	def substitute_color_tags(data)
		data = data.gsub(%r{<b>(.*?)</b>}m, "\e[1m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<red>(.*?)</red>}m, "\e[1m\e[31m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<green>(.*?)</green>}m, "\e[1m\e[32m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<yellow>(.*?)</yellow>}m, "\e[1m\e[33m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<banner>(.*?)</banner>}m, "\e[33m\e[44m\e[1m\\1#{DEFAULT_TERMINAL_COLORS}")
		return data
	end
	
	def reset_terminal_colors
		STDOUT.write("\e[0m")
		STDOUT.flush
	end
	
	def line
		puts "--------------------------------------------"
	end
	
	def wait
		STDIN.readline
	rescue Interrupt
		exit 2
	end
	
	def query_directory(default = "")
		while true
			STDOUT.write("[#{default}] : ")
			STDOUT.flush
			input = STDIN.readline.strip
			if input.empty?
				return default
			elsif input !~ /^\//
				color_puts "<red>Please specify an absolute directory.</red>"
			elsif input =~ /\s/
				color_puts "<red>The directory name may not contain spaces.</red>"
			else
				return input
			end
		end
	rescue Interrupt, EOFError
		exit 2
	end
	
	def sh(*command)
		puts command.join(' ')
		return system(*command)
	end
	
	def configure_autoconf_package(dir, name, configure_options = nil)
		line
		color_puts "<banner>Compiling and optimizing #{name}</banner>"
		color_puts "In the mean time, feel free to grab a cup of coffee.\n\n"
		Dir.chdir(dir) do
			# If nothing has been compiled yet, then update the timestamps of the files.
			# This prevents 'configure' and 'make' from thinking that the 'configure'
			# script must be regenerated.
			if Dir["*.o"].empty?
				system("touch *")
			end
			
			if @prefix_changed || !File.exist?("Makefile")
				if !sh("./configure --prefix=#{@prefix} #{configure_options}")
					return false
				end
			else
				color_puts "<green>It looks like the source is already configured.</green>"
				color_puts "<green>Skipping configure script...</green>"
			end
			return true
		end
	end
	
	def install_autoconf_package(dir, name)
		Dir.chdir(dir) do
			if block_given?
				result = yield
			else
				result = sh("make install")
			end
			if result
				return true
			else
				puts
				line
				color_puts "<red>Cannot install #{name}</red>"
				puts
				color_puts "This installer was able to compile #{name}, but could not"
				color_puts "install the files to <b>#{@prefix}</b>."
				puts
				if Process.uid == 0
					color_puts "This installer probably doesn't have permission " <<
						"to write to that folder, even though it's running as " <<
						"root. Please fix the permissions, and re-run this installer."
				else
					color_puts "This installer probably doesn't have permission to " <<
						"write to that folder, because it's running as " <<
						"<b>#{`whoami`.strip}</b>. Please re-run this installer " <<
						"as <b>root</b>."
				end
				return false
			end
		end
	end
end

Installer.new.start
