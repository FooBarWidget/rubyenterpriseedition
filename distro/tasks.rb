REE_VERSION = "20080317"

desc "Create a distribution package"
task :package do
	ruby_version = read_ruby_version
	distdir = "ruby-enterprise-#{ruby_version}-#{REE_VERSION}"
	sh "rm -rf #{distdir}"
	sh "mkdir #{distdir}"
	
	sh "mkdir #{distdir}/source"
	sh "git archive HEAD | tar -C #{distdir}/source -xf -"
	Dir.chdir("#{distdir}/source") do
		sh "autoconf"
		sh 'rm', '-rf', 'autom4te.cache'
		system 'bison', '-y', '-o', 'parse.c', 'parse.y'
	end
	
	sh "cp distro/installer distro/installer.rb #{distdir}/"
	File.open("#{distdir}/version.txt", "w") do |f|
		f.write("#{ruby_version}-#{REE_VERSION}")
	end
	
	if Dir["distro/rubygems*.tgz"].empty?
		Dir.chdir("distro") do
			sh "wget", "http://rubyforge.org/frs/download.php/29548/rubygems-1.0.1.tgz"
		end
	end
	rubygems_archive = File.expand_path(Dir["distro/rubygems*.tgz"].first)
	Dir.chdir(distdir) do
		sh "tar", "xzf", rubygems_archive
		sh "mv rubygems* rubygems"
	end
	
	ENV['GZIP'] = '--best'
	sh "tar -czf #{distdir}.tar.gz #{distdir}"
	sh "rm -rf #{distdir}"
end

def read_ruby_version
	data = File.read("version.h")
	data =~ /RUBY_VERSION "(.*)"/
	return $1
end
