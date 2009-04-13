VENDOR_RUBY_VERSION = begin
	data = File.read("version.h")
	data =~ /RUBY_VERSION "(.*)"/
	$1
end
REE_VERSION = begin
	data = File.read("version.c")
	data =~ /REE_VERSION "(.*)"/
	$1
end
DISTDIR = "ruby-enterprise-#{VENDOR_RUBY_VERSION}-#{REE_VERSION}"
RUBYGEMS_URL = "http://rubyforge.org/frs/download.php/45905/rubygems-1.3.1.tgz"
RUBYGEMS_PACKAGE = RUBYGEMS_URL.sub(/.*\//, '')

desc "Create a distribution directory"
task :distdir do
	create_distdir
end

desc "Create a distribution package"
task :package do
	create_distdir
	ENV['GZIP'] = '--best'
	sh "tar -czf #{DISTDIR}.tar.gz #{DISTDIR}"
	sh "rm -rf #{DISTDIR}"
end

desc "Test the installer script. Pass extra arguments to the installer with ARGS."
task :test_installer do
	distdir = "/tmp/r8ee-test"
	create_distdir(distdir)
	sh "#{distdir}/installer #{ENV['ARGS']}"
end

desc "Auto-install into a fake root directory"
task :fakeroot do
	create_fakeroot
end

desc "Create a Debian package. Set SKIP_FAKEROOT=1 if you want to bypass fakeroot recreation."
task 'package:debian' do
	create_fakeroot if ENV['SKIP_FAKEROOT'].nil? || ENV['SKIP_FAKEROOT'].empty?
	sh "rm -rf fakeroot/DEBIAN"
	sh "cp -R distro/debian fakeroot/DEBIAN"
	
	installed_size = disk_usage("fakeroot")
	raw_arch = `uname -m`.strip
	arch = case raw_arch
	when /^i.86$/
		"i386"
	when /^x86_64/
		"amd64"
	else
		raw_arch
	end
	
	require 'erb'
	template = ERB.new(File.read("fakeroot/DEBIAN/control"))
	File.open("fakeroot/DEBIAN/control", "w") do |f|
		f.write(template.result(binding))
	end
	
	sh "fakeroot dpkg -b fakeroot ruby-enterprise_#{VENDOR_RUBY_VERSION}-#{REE_VERSION}_#{arch}.deb"
end

desc "Generate the documentation HTML"
file 'distro/documentation.html' => 'distro/documentation.txt' do
	sh "asciidoc -a toc -a numbered -a toclevels=3 -a icons distro/documentation.txt"
end

def create_fakeroot
	distdir = "/tmp/r8ee-test"
	create_distdir(distdir)
	sh "rm -rf fakeroot"
	sh "mkdir fakeroot"
	fakeroot = File.expand_path("fakeroot")
	sh "#{distdir}/installer --auto='/opt/ruby-enterprise' --destdir='#{fakeroot}' #{ENV['ARGS']}"
	each_elf_binary(fakeroot) do |filename|
		sh "strip --strip-debug '#{filename}'"
	end
	puts "*** Ruby Enterprise Edition has been installed to #{fakeroot}"
end

# Check whether the specified command is in $PATH, and return its
# absolute filename. Returns nil if the command is not found.
#
# This function exists because system('which') doesn't always behave
# correctly, for some weird reason.
def find_command(name)
	ENV['PATH'].split(File::PATH_SEPARATOR).detect do |directory|
		path = File.join(directory, name.to_s)
		if File.executable?(path)
			return path
		end
	end
	return nil
end

def download(url)
	if find_command('wget')
		sh "wget", RUBYGEMS_URL
	else
		sh "curl", "-O", "-L", RUBYGEMS_URL
	end
end

def create_distdir(distdir = DISTDIR)
	sh "rm -rf #{distdir}"
	sh "mkdir #{distdir}"
	
	sh "mkdir #{distdir}/source"
	sh "git archive HEAD | tar -C #{distdir}/source -xf -"
	Dir.chdir("#{distdir}/source") do
		sh "autoconf"
		sh 'rm', '-rf', 'autom4te.cache'
		sh 'bison', '-y', '-o', 'parse.c', 'parse.y'
	end
	
	sh "cp distro/installer distro/installer.rb distro/platform_info.rb " <<
		"distro/dependencies.rb distro/optparse.rb #{distdir}/"
	sh "cd #{distdir} && ln -s source/distro/runtime ."
	File.open("#{distdir}/version.txt", "w") do |f|
		f.write("#{VENDOR_RUBY_VERSION}-#{REE_VERSION}")
	end
	
	if !File.exist?("distro/#{RUBYGEMS_PACKAGE}")
		Dir.chdir("distro") do
			download(RUBYGEMS_URL)
		end
	end
	rubygems_package = File.expand_path("distro/#{RUBYGEMS_PACKAGE}")
	Dir.chdir(distdir) do
		sh "tar", "xzf", rubygems_package
		sh "mv rubygems* rubygems"
	end
end

# Returns whether the given filename is an ELF binary.
def elf_binary?(filename)
	if File.executable?(filename)
		return File.read(filename, 4) == "\177ELF"
	else
		return false
	end
end

# Iterates through each ELF binary file in the given directory.
def each_elf_binary(dir, &block)
	Dir["#{dir}/*"].each do |filename|
		if File.directory?(filename)
			each_elf_binary(filename, &block)
		elsif elf_binary?(filename)
			block.call(filename)
		end
	end
end

# Returns the disk usage of the given directory, in KB.
def disk_usage(dir)
	if RUBY_PLATFORM =~ /linux/
		options = "-a -k --apparent-size --max-depth=0"
	else
		options = "-k -d 0"
	end
	return `du #{options} \"#{dir}\"`.strip.to_i
end