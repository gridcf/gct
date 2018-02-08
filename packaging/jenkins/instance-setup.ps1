# This script is initializes a Windows AMI with the build environment for
# building Globus for 32-bit or 64-bit windows, either natively
# with MingW64 or using the Cygwin runtime.
param (
    [string]$InstanceType = $(throw "-InstanceType is required")
)
Start-Transcript -path C:\Windows\Temp\instance-setup.txt

$password = [Guid]::NewGuid().Guid
Net user /add jenkins "$password" /yes

$cygwin_core_packages = "autoconf,autoconf2.5,automake,automake1.14,automake1.15,bison,bsdtar,cpio,curl,diffutils,dos2unix,doxygen,findutils,flex,gawk,gccmakedep,gdb,git,graphviz,grep,groff,libtool,make,makedepend,openssh,openssl,patch,patchutils,pax,perl-Test-Simple,perl-XML-Simple,pylint,python,rebase,rpm,vim,vim-common,vim-minimal,w3m,zip,zsh"

$cygwin_dev_packages = "binutils,bzip2,gcc-core,gcc-g++,gcc-tools-autoconf,gcc-tools-automake,gcc4,gcc4-core,gcc4-g++,gettext,gettext-devel,glib2.0-networking,graphviz,grep,groff,libisl10,libcloog-isl4,libffi-devel,libffi6,libgcc1,libglib2.0-devel,libglib2.0_0,libnice-devel,openssl-devel,pkg-config,zlib,zlib-devel"

$mingw_32_packages = "mingw64-i686-binutils,mingw64-i686-bzip2,mingw64-i686-gcc-core,mingw64-i686-gcc-g++,mingw64-i686-headers,mingw64-i686-pkg-config,mingw64-i686-runtime,mingw64-i686-windows-default-manifest,mingw64-i686-xz,mingw64-i686-zlib"

$mingw_64_packages = "mingw64-x86_64-binutils,mingw64-x86_64-bzip2,mingw64-x86_64-gcc-core,mingw64-x86_64-gcc-g++,mingw64-x86_64-headers,mingw64-x86_64-pkg-config,mingw64-x86_64-runtime,mingw64-x86_64-windows-default-manifest,mingw64-x86_64-xz,mingw64-x86_64-zlib"

switch ($InstanceType)
{
    "mingw32"  {
        $packages = "$cygwin_core_packages,$mingw_32_packages"
        $cygwin_setup = "setup-x86.exe"
    }
    "mingw64"  {
        $packages = "$cygwin_core_packages,$mingw_64_packages"
        $cygwin_setup = "setup-x86_64.exe"
    }
    "cygwin32" {
        $packages = "$cygwin_core_packages,$cygwin_dev_packages"
        $cygwin_setup = "setup-x86.exe"
    }
    "cygwin64" {
        $packages = "$cygwin_core_packages,$cygwin_dev_packages"
        $cygwin_setup = "setup-x86_64.exe"
    }
}

Echo "Downloading Cygwin setup"
Invoke-WebRequest `
        -Uri "https://cygwin.com/${cygwin_setup}" `
        -OutFile "C:\Windows\Temp\${cygwin_setup}"

mkdir "C:\Windows\Temp\Cygwin"


Echo "Installing cygwin"
$cygwinInstallProc = Start-Process -NoNewWindow `
        "C:\Windows\Temp\${cygwin_setup}" `
        -ArgumentList "-R C:\cygwin -s http://mirrors.kernel.org/sourceware/cygwin -A -q -l C:\Windows\Temp\cygwin -P `"$packages`"" `
        -Wait -RedirectStandardOutput "C:\Windows\Temp\setup-log.txt"

if ($InstanceType -eq "mingw32" -or $InstanceType -eq "mingw64")
{
    $mingw_repo_m="https://dl.fedoraproject.org/pub/epel/7/x86_64/Packages/m/"
    $mingw_prereqs = `
        "${InstanceType}-gettext",`
        "${InstanceType}-pcre",`
        "${InstanceType}-glib2",`
        "${InstanceType}-glib-networking",`
        "${InstanceType}-libffi",`
        "${InstanceType}-win-iconv",`
        "${InstanceType}-winpthreads"

    Echo "Installing additional prereqs ($mingw_prereqs) for mingw from EPEL 7"
    Echo "Fetching x86_64/m dir index"
    $info = Invoke-WebRequest -Uri "${mingw_repo_m}"
    foreach ($prereq in $mingw_prereqs) {
        $hrefs = ($info.Links | Where {($_.href.StartsWith(${prereq})) -and ($_.href.EndsWith(".rpm"))}).href
        foreach ($href in $hrefs) {
            if ($href.StartsWith("http")) {
                $abs_uri = "${href}"
            }
            else {
                $abs_uri = "${mingw_repo_m}/${href}"
            }
            $abs_file = "C:\Windows\Temp\${prereq}.rpm"
            Echo "Fetching ${abs_uri} and saving to ${abs_file}"
            Invoke-WebRequest -Uri "$abs_uri" -OutFile "$abs_file"
            $cygpath = (C:\cygwin\bin\cygpath.exe "$abs_file")
            C:\cygwin\bin\bash.exe --login -c "/bin/rpm2cpio '${cygpath}' | (cd /; cpio -id)"
        }
    }
}

Echo "Downloading JRE"
Invoke-WebRequest `
        -Uri "http://builds.globus.org/jre-install.exe" `
        -OutFile "C:\Windows\Temp\jvm-installer.exe"

New-Item -Type file -Path "C:\Windows\Temp\jvm.properties" -Force `
    -Value "INSTALLDIR=C:\Java"

Echo "Installing JRE"
Start-Process "C:\Windows\Temp\jvm-installer.exe" `
        -ArgumentList "/s INSTALLCFG=C:\Windows\Temp\jvm.properties" `
        -Wait

Echo "Configuring ssh public key"
mkdir C:\cygwin\home\jenkins\.ssh
mkdir C:\cygwin\home\Administrator\.ssh

Echo "Downloading ssh public key"
$public_key = (Invoke-WebRequest `
    -Uri "http://169.254.169.254/latest/meta-data/public-keys/0/openssh-key")

[IO.File]::WriteAllLines("c:\cygwin\home\jenkins\.ssh\authorized_keys", `
        $public_key.Content)
C:\Cygwin\bin\bash --login -c "chown jenkins ~jenkins/.ssh ; chmod -R og-rw ~jenkins/.ssh"

[IO.File]::WriteAllLines("c:\cygwin\home\Administrator\.ssh\authorized_keys", `
        $public_key.Content)
C:\Cygwin\bin\bash --login -c "chown -R Administrator ~; chmod -R og-rw ~"

Echo "Downloading tap-to-junit-xml"
mkdir C:\cygwin\home\jenkins\bin
Invoke-WebRequest `
        -Uri "http://builds.globus.org/gt6/etc/tap-to-junit-xml" `
        -OutFile "C:\cygwin\home\jenkins\bin\tap-to-junit-xml"

C:\Cygwin\bin\bash --login -c "chown -R jenkins ~jenkins;  chmod a+x ~jenkins/bin/*; chmod -R og-rw ~jenkins;"

# There's some issue with running ssh-host-config without being logged into
# console--the cyg_server doesn't get created correctly. Manually create the
# user, setting rights to do token magic and add to the "Administrators" group
# so that seteuid works.
$password = [Guid]::NewGuid().Guid
Net user /add cyg_server "$password" /yes

Echo "Adding cyg_server to Administrators group"
net localgroup Administrators cyg_server /add

Echo "Setting cyg_server password to never expire"
C:\cygwin\bin\bash.exe --login -c "passwd -e cyg_server"

$rights = "SeAssignPrimaryTokenPrivilege",`
          "SeCreateTokenPrivilege",`
          "SeTcbPrivilege",`
          "SeDenyInteractiveLogonRight",`
          "SeDenyRemoteInteractiveLogonRight",`
          "SeServiceLogonRight"

foreach ($right in $rights) {
    Echo "Adding $right right to cyg_server account"
    C:\cygwin\bin\bash.exe --login -c "editrights -u cyg_server -a $right"
}

C:\cygwin\bin\bash.exe --login -c "/bin/ssh-host-config -u cyg_server -y -w '$password' 2>&1 | tee /cygdrive/c/Windows/Temp/openssh-install.log"

Echo "Adding java to the default path"
setx PATH "$env:Path;C:\Java\bin" /M

Echo "Setting cyg_user homedir to /var/empty"
net user cyg_server /homedir:C:\Cygwin\var\empty

Echo "Adding firewall rule"
New-NetFirewallRule -Protocol TCP -LocalPort 22 -Direction Inbound -Action Allow -DisplayName SSH

Echo "Installing XML::Generator perl module"
C:\cygwin\bin\bash.exe --login -c "(echo y;echo o conf prerequisites_policy follow;echo o conf commit)|cpan; cpan install XML::Generator"

Echo "Adding IP to /etc/hosts"
$public_hostname = (Invoke-WebRequest `
        -Uri http://169.254.169.254/latest/meta-data/public-hostname).Content

$ip_address = (Invoke-WebRequest `
        -Uri http://169.254.169.254/latest/meta-data/local-ipv4).Content

C:\cygwin\bin\bash.exe --login -c "printf '%s\r\n' '$ip_address $public_hostname' >> /etc/hosts"

Echo "Starting OpenSSH server"
Start-Service sshd

Stop-Transcript
