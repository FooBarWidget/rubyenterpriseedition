require "#{File.dirname(__FILE__)}/platform_info"
module RubyEnterpriseEdition

# Represents a dependency software that Ruby Enterprise Edition requires. It's used by the
# installer to check whether all dependencies are available. A Dependency object
# contains full information about a dependency, such as its name, code for
# detecting whether it is installed, and installation instructions for the
# current platform.
class Dependency # :nodoc: all
	[:name, :install_command, :install_instructions, :install_comments,
	 :website, :website_comments, :provides].each do |attr_name|
		attr_writer attr_name
		
		define_method(attr_name) do
			call_init_block
			return instance_variable_get("@#{attr_name}")
		end
	end
	
	def initialize(&block)
		@included_by = []
		@init_block = block
	end
	
	def define_checker(&block)
		@checker = block
	end
	
	def check
		call_init_block
		result = Result.new
		@checker.call(result)
		return result
	end

private
	class Result
		def found(filename_or_boolean = nil)
			if filename_or_boolean.nil?
				@found = true
			else
				@found = filename_or_boolean
			end
		end
		
		def not_found
			found(false)
		end
		
		def found?
			return !@found.nil? && @found
		end
		
		def found_at
			if @found.is_a?(TrueClass) || @found.is_a?(FalseClass)
				return nil
			else
				return @found
			end
		end
	end

	def call_init_block
		if @init_block
			init_block = @init_block
			@init_block = nil
			init_block.call(self)
		end
	end
end

# Namespace which contains the different dependencies that Ruby Enterprise Edition may require.
# See Dependency for more information.
module Dependencies # :nodoc: all
	include PlatformInfo
	
	CC = Dependency.new do |dep|
		dep.name = "C compiler"
		dep.define_checker do |result|
			if PlatformInfo::CC.nil?
				result.not_found
			else
				result.found(PlatformInfo::CC)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install build-essential"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install gcc-c++"
			when :gentoo
				dep.install_command = "emerge -av gcc"
			end
		elsif RUBY_PLATFORM =~ /darwin/
			dep.install_instructions = "Please install the Apple Development Tools: http://developer.apple.com/tools/"
		end
		dep.website = "http://gcc.gnu.org/"
	end
	
	CXX = Dependency.new do |dep|
		dep.name = "C++ compiler"
		dep.define_checker do |result|
			if PlatformInfo::CXX.nil?
				result.not_found
			else
				result.found(PlatformInfo::CXX)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install build-essential"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install gcc-c++"
			when :gentoo
				dep.install_command = "emerge -av gcc"
			end
		elsif RUBY_PLATFORM =~ /darwin/
			dep.install_instructions = "Please install the Apple Development Tools: http://developer.apple.com/tools/"
		end
		dep.website = "http://gcc.gnu.org/"
	end
	
	Zlib_Dev = Dependency.new do |dep|
		dep.name = "Zlib development headers"
		dep.define_checker do |result|
			begin
				File.open('/tmp/r8ee-check.c', 'w') do |f|
					f.write("#include <zlib.h>")
				end
				Dir.chdir('/tmp') do
					if system("(#{PlatformInfo::CC || 'gcc'} #{ENV['CFLAGS']} -c r8ee-check.c) >/dev/null 2>/dev/null")
						result.found
					else
						result.not_found
					end
				end
			ensure
				File.unlink('/tmp/r8ee-check.c') rescue nil
				File.unlink('/tmp/r8ee-check.o') rescue nil
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install zlib1g-dev"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install zlib-devel"
			end
		end
		dep.website = "http://www.zlib.net/"
	end
	
	OpenSSL_Dev = Dependency.new do |dep|
		dep.name = "OpenSSL development headers"
		dep.define_checker do |result|
			begin
				File.open('/tmp/r8ee-check.c', 'w') do |f|
					f.write("#include <openssl/ssl.h>")
				end
				Dir.chdir('/tmp') do
					if system("(#{PlatformInfo::CC || 'gcc'} #{ENV['CFLAGS']} -c r8ee-check.c) >/dev/null 2>/dev/null")
						result.found
					else
						result.not_found
					end
				end
			ensure
				File.unlink('/tmp/r8ee-check.c') rescue nil
				File.unlink('/tmp/r8ee-check.o') rescue nil
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install libssl-dev"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install openssl-devel"
			end
		end
		dep.website = "http://www.openssl.org/"
	end
	
	Readline_Dev = Dependency.new do |dep|
		dep.name = "GNU Readline development headers"
		dep.define_checker do |result|
			begin
				File.open('/tmp/r8ee-check.c', 'w') do |f|
					# readline.h doesn't work on OS X unless we #include stdio.h
					f.puts("#include <stdio.h>")
					f.puts("#include <readline/readline.h>")
				end
				Dir.chdir('/tmp') do
					if system("(#{PlatformInfo::CC || 'gcc'} #{ENV['CFLAGS']} -c r8ee-check.c) >/dev/null 2>/dev/null")
						result.found
					else
						result.not_found
					end
				end
			ensure
				File.unlink('/tmp/r8ee-check.c') rescue nil
				File.unlink('/tmp/r8ee-check.o') rescue nil
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install libreadline5-dev"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install readline-devel"
			end
		end
		dep.website = "http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html"
	end
end

end # module RubyEnterpriseEdition
