#!/usr/bin/env ruby
require "#{File.dirname(__FILE__)}/optparse"
require "#{File.dirname(__FILE__)}/dependencies"

class Installer
	include RubyEnterpriseEdition

	ROOT = File.expand_path(File.dirname(__FILE__))
	PASSENGER_WEBSITE = "http://www.modrails.com"
	EMM_RUBY_WEBSITE = "http://www.rubyenterpriseedition.com"
	REQUIRED_DEPENDENCIES = [
		Dependencies::GCC,
		Dependencies::Zlib_Dev,
		Dependencies::OpenSSL_Dev
	]
	
	def start(auto_install_prefix = nil, destdir = nil)
		Dir.chdir(ROOT)
		@version = File.read("version.txt")
		@auto_install_prefix = auto_install_prefix
		@destdir = destdir
		show_welcome_screen
		check_dependencies
		ask_installation_prefix
		
		steps = []
		if tcmalloc_supported?
			if libunwind_needed?
				steps += [
				  :configure_libunwind,
				  :compile_libunwind,
				  :install_libunwind
				]
			end
			steps += [
			  :configure_tcmalloc,
			  :compile_tcmalloc,
			  :install_tcmalloc
			]
		end
		steps += [
		  :configure_ruby,
		  :compile_ruby,
		  :install_ruby,
		  :install_rubygems
		]
		steps.each do |step|
			if !self.send(step)
				exit 1
			end
		end
		install_useful_libraries
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
		missing_dependencies = []
		color_puts "<banner>Checking for required software...</banner>"
		puts
		REQUIRED_DEPENDENCIES.each do |dep|
			color_print " * #{dep.name}... "
			result = dep.check
			if result.found?
				if result.found_at
					color_puts "<green>found at #{result.found_at}</green>"
				else
					color_puts "<green>found</green>"
				end
			else
				color_puts "<red>not found</red>"
				missing_dependencies << dep
			end
		end
		
		if missing_dependencies.empty?
			return true
		else
			puts
			color_puts "<red>Some required software is not installed.</red>"
			color_puts "But don't worry, this installer will tell you how to install them.\n"
			color_puts "<b>Press Enter to continue, or Ctrl-C to abort.</b>"
			wait
			
			line
			color_puts "<banner>Installation instructions for required software</banner>"
			puts
			missing_dependencies.each do |dep|
				print_dependency_installation_instructions(dep)
				puts
			end
			return false
		end
	end
	
	def ask_installation_prefix
		line
		color_puts "<banner>Target directory</banner>"
		puts
		@old_prefix = File.read("source/.prefix.txt") rescue nil
		if @auto_install_prefix
			@prefix = @auto_install_prefix
			puts "Auto-installing to: #{@prefix}"
		else
			puts "Where would you like to install Ruby Enterprise Edition to?"
			puts "(All Ruby Enterprise Edition files will be put inside that directory.)"
			puts
			@prefix = query_directory(@old_prefix || "/opt/ruby-enterprise-#{@version}")
		end
		@prefix_changed = @prefix != @old_prefix
		File.open("source/.prefix.txt", "w") do |f|
			f.write(@prefix)
		end
		
		if @destdir && !@destdir =~ /\/$/
			@destdir += "/"
		end
		
		ENV['C_INCLUDE_PATH'] = "#{@destdir}#{@prefix}/include:/usr/include:/usr/local/include:#{ENV['C_INCLUDE_PATH']}"
		ENV['CPLUS_INCLUDE_PATH'] = "#{@destdir}#{@prefix}/include:/usr/include:/usr/local/include:#{ENV['CPLUS_INCLUDE_PATH']}"
		ENV['LD_LIBRARY_PATH'] = "#{@destdir}#{@prefix}/lib:#{ENV['LD_LIBRARY_PATH']}"
	end
	
	def configure_libunwind
		return configure_autoconf_package('source/vendor/libunwind-0.98.6',
			'libunwind')
	end
	
	def compile_libunwind
		Dir.chdir('source/vendor/libunwind-0.98.6') do
			return sh("make")
		end
	end
	
	def install_libunwind
		return install_autoconf_package('source/vendor/libunwind-0.98.6',
		  'libunwind')
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
			sh("mkdir", "-p", "#{@destdir}#{@prefix}/lib") &&
			if RUBY_PLATFORM =~ /darwin/
				lib_extension = "dylib"
			else
				lib_extension = "so"
			end
			sh("cp .libs/libtcmalloc_minimal.#{lib_extension}* '#{@destdir}#{@prefix}/lib/'")
		end
	end
	
	def configure_ruby
		return configure_autoconf_package('source', 'Ruby Enterprise Edition')
	end
	
	def compile_ruby
		Dir.chdir("source") do
			# No idea why, but sometimes 'make' fails unless we do this.
			sh("mkdir -p .ext/common")
			
			if tcmalloc_supported?
				return sh("make EXTLIBS='-Wl,-rpath,#{@prefix}/lib -L#{@destdir}#{@destdir}/lib -ltcmalloc_minimal'")
			else
				return sh("make")
			end
		end
	end
	
	def install_ruby
		return install_autoconf_package('source', 'Ruby Enterprise Edition')
	end
	
	def install_rubygems
		basedir = "#{@destdir}#{@prefix}/lib/ruby"
		libdir = "#{basedir}/1.8"
		archname = File.basename(File.dirname(Dir["#{libdir}/*/thread.so"].first))
		extlibdir = "#{libdir}/#{archname}"
		site_libdir = "#{basedir}/site_ruby/1.8"
		site_extlibdir = "#{site_libdir}/#{archname}"
		
		ENV['RUBYLIB'] = "#{libdir}:#{extlibdir}:#{site_libdir}:#{site_extlibdir}"
		puts ENV['RUBYLIB']
		
		Dir.chdir("rubygems") do
			line
			color_puts "<banner>Installing RubyGems...</banner>"
			return sh("#{@destdir}#{@prefix}/bin/ruby", "setup.rb", "--no-ri", "--no-rdoc")
		end
	end
	
	def install_useful_libraries
		line
		color_puts "<banner>Installing useful libraries...</banner>"
		gem = "#{@destdir}#{@prefix}/bin/ruby #{@destdir}#{@prefix}/bin/gem"
		
		gem_names = ["rails", "fastthread", "rack", "mysql", "sqlite3-ruby", "postgres"]
		failed_gems = []
		
		if sh("#{gem} sources --update")
			gem_names.each do |gem_name|
				color_puts "\n<b>Installing #{gem_name}...</b>"
				if !sh("#{gem} install --no-rdoc --no-ri --no-update-sources --backtrace #{gem_name}")
					failed_gems << gem_name
				end
			end
		else
			failed_gems = gem_names
		end
		
		if !failed_gems.empty?
			line
			color_puts "<banner>Warning: some libraries could not be installed</banner>"
			color_puts "The following gems could not be installed, probably because of an Internet"
			color_puts "connection error:"
			puts
			failed_gems.each do |gem_name|
				color_puts " <b>* #{gem_name}</b>"
			end
			puts
			color_puts "These gems are not required, i.e. Ruby Enterprise Edition will work fine without them. But most people use Ruby Enterprise Edition in combination with Phusion Passenger and Ruby on Rails, which do require one or more of the aforementioned gems, so you may want to install them later."
			puts
			color_puts "To install the aforementioned gems, please use the following command:"
			color_puts "<yellow>#{gem} install #{failed_gems.join(' ')}</yellow>"
			puts
			color_puts "<b>Press ENTER to show the next screen.</b>"
			wait
		end
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
		color_puts "Enjoy Ruby Enterprise Edition, a product of <yellow>Phusion (www.phusion.nl)</yellow> :-)"
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
		if !@auto_install_prefix
			STDIN.readline
		end
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
	
	def tcmalloc_supported?
		return !platform_is_64_bit?
	end
	
	def libunwind_needed?
		return false
	end
	
	def platform_is_64_bit?
		machine = `uname -m`.strip
		return machine == "x86_64"
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
				result = sh("make install DESTDIR='#{@destdir}'")
			end
			if result
				return true
			else
				puts
				line
				color_puts "<red>Cannot install #{name}</red>"
				puts
				color_puts "This installer was able to compile #{name}, but could not " <<
					"install the files to <b>#{@destdir}#{@prefix}</b>."
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

options = {}
parser = OptionParser.new do |opts|
	opts.banner = "Usage: installer [options]"
	opts.separator("")
	
	opts.on("-a", "--auto PREFIX", String,
	"Automatically install to directory PREFIX\n#{' ' * 37}without any user interaction") do |dir|
		options[:prefix] = dir
	end
	
	opts.on("--destdir DIR", String) do |dir|
		options[:destdir] = dir
	end
	
	opts.on("-h", "--help", "Show this message") do
		puts opts
		exit
	end
end
begin
	parser.parse!
rescue OptionParser::ParseError => e
	puts e
	puts
	puts "Please see '--help' for valid options."
	exit 1
end

Installer.new.start(options[:prefix], options[:destdir])
