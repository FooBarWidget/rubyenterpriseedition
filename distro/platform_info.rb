# This module autodetects various platform-specific information, and
# provides that information through constants.
module PlatformInfo
private
	def self.env_defined?(name)
		return !ENV[name].nil? && !ENV[name].empty?
	end
	
	def self.determine_gem_command
		gem_exe_in_path = find_command("gem")
		correct_gem_exe = File.dirname(RUBY) + "/gem"
		if gem_exe_in_path.nil? || gem_exe_in_path == correct_gem_exe
			return "gem"
		else
			return correct_gem_exe
		end
	end

	def self.find_apxs2
		if env_defined?("APXS2")
			return ENV["APXS2"]
		end
		['apxs2', 'apxs'].each do |name|
			command = find_command(name)
			if !command.nil?
				return command
			end
		end
		return nil
	end
	
	def self.determine_apache2_bindir
		if APXS2.nil?
			return nil
		else
			return `#{APXS2} -q BINDIR 2>/dev/null`.strip
		end
	end
	
	def self.determine_apache2_sbindir
		if APXS2.nil?
			return nil
		else
			return `#{APXS2} -q SBINDIR`.strip
		end
	end
	
	def self.find_apache2_executable(*possible_names)
		[APACHE2_BINDIR, APACHE2_SBINDIR].each do |bindir|
			if bindir.nil?
				next
			end
			possible_names.each do |name|
				filename = "#{bindir}/#{name}"
				if File.file?(filename) && File.executable?(filename)
					return filename
				end
			end
		end
		return nil
	end
	
	def self.find_apache2ctl
		return find_apache2_executable('apache2ctl', 'apachectl')
	end
	
	def self.find_httpd
		if env_defined?('HTTPD')
			return ENV['HTTPD']
		elsif APXS2.nil?
			["apache2", "httpd2", "apache", "httpd"].each do |name|
				command = find_command(name)
				if !command.nil?
					return command
				end
			end
			return nil
		else
			return find_apache2_executable(`#{APXS2} -q TARGET`.strip)
		end
	end
	
	def self.determine_apxs2_flags
		if APXS2.nil?
			return nil
		else
			flags = `#{APXS2} -q CFLAGS`.strip << " -I" << `#{APXS2} -q INCLUDEDIR`
			flags.strip!
			flags.gsub!(/-O\d? /, '')
			return flags
		end
	end
	
	def self.find_apr_config
		if env_defined?('APR_CONFIG')
			apr_config = ENV['APR_CONFIG']
		elsif RUBY_PLATFORM =~ /darwin/ && HTTPD == "/usr/sbin/httpd"
			# If we're on MacOS X, and we're compiling against the
			# default provided Apache, then we'll want to query the
			# correct 'apr-1-config' command. However, that command
			# is not in $PATH by default. Instead, it lives in
			# /Developer/SDKs/MacOSX*sdk/usr/bin.
			sdk_dir = Dir["/Developer/SDKs/MacOSX*sdk"].sort.last
			if sdk_dir
				apr_config = "#{sdk_dir}/usr/bin/apr-1-config"
				if !File.executable?(apr_config)
					apr_config = nil
				end
			end
		else
			apr_config = find_command('apr-1-config')
			if apr_config.nil?
				apr_config = find_command('apr-config')
			end
		end
		return apr_config
	end
	
	def self.determine_apr_info
		if APR_CONFIG.nil?
			return nil
		else
			flags = `#{APR_CONFIG} --cppflags --includes`.strip
			libs = `#{APR_CONFIG} --link-ld`.strip
			flags.gsub!(/-O\d? /, '')
			return [flags, libs]
		end
	end
	
	def self.determine_multi_arch_flags
		if RUBY_PLATFORM =~ /darwin/ && !HTTPD.nil?
			architectures = []
			`file "#{HTTPD}"`.split("\n").grep(/for architecture/).each do |line|
				line =~ /for architecture (.*?)\)/
				architectures << "-arch #{$1}"
			end
			return architectures.join(' ')
		else
			return ""
		end
	end
	
	def self.determine_library_extension
		if RUBY_PLATFORM =~ /darwin/
			return "bundle"
		else
			return "so"
		end
	end
	
	def self.read_file(filename)
		return File.read(filename)
	rescue
		return ""
	end
	
	def self.determine_libext
		if RUBY_PLATFORM =~ /darwin/
			return "dylib"
		else
			return "so"
		end
	end
	
	def self.determine_ruby_libext
		if RUBY_PLATFORM =~ /darwin/
			return "bundle"
		else
			return "so"
		end
	end
	
	def self.determine_linux_distro
		if RUBY_PLATFORM !~ /linux/
			return nil
		end
		lsb_release = read_file("/etc/lsb-release")
		if lsb_release =~ /Ubuntu/
			return :ubuntu
		elsif File.exist?("/etc/debian_version")
			return :debian
		elsif File.exist?("/etc/redhat-release")
			redhat_release = read_file("/etc/redhat-release")
			if redhat_release =~ /CentOS/
				return :centos
			elsif redhat_release =~ /Fedora/  # is this correct?
				return :fedora
			else
				# On official RHEL distros, the content is in the form of
				# "Red Hat Enterprise Linux Server release 5.1 (Tikanga)"
				return :rhel
			end
		elsif File.exist?("/etc/suse-release")
			return :suse
		elsif File.exist?("/etc/gentoo-release")
			return :gentoo
		else
			return :unknown
		end
		# TODO: Slackware, Mandrake/Mandriva
	end
	
	def self.determine_c_compiler
		return ENV['CC'] || find_command('gcc') || find_command('cc')
	end
	
	def self.determine_cxx_compiler
		return ENV['CXX'] || find_command('g++') || find_command('c++')
	end
	
	# Returns true if the Solaris version of ld is in use.
	def self.solaris_ld?
		ld_version = `ld -V 2>&1`
		return !!ld_version.index("Solaris")
	end

public
	# Check whether the specified command is in $PATH, and return its
	# absolute filename. Returns nil if the command is not found.
	#
	# This function exists because system('which') doesn't always behave
	# correctly, for some weird reason.
	def self.find_command(name)
		ENV['PATH'].split(File::PATH_SEPARATOR).detect do |directory|
			path = File.join(directory, name.to_s)
			if File.executable?(path)
				return path
			end
		end
		return nil
	end
	
	CC = determine_c_compiler
	CXX = determine_cxx_compiler
	LIBEXT = determine_libext
	RUBYLIBEXT = determine_ruby_libext

	# An identifier for the current Linux distribution. nil if the operating system is not Linux.
	LINUX_DISTRO = determine_linux_distro
end
