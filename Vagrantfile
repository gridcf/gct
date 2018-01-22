# -*- mode: ruby -*-
# vi: set ft=ruby :

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure(2) do |config|
  config.vm.define "gctbuild"
  # The most common configuration options are documented and commented below.
  # For a complete reference, please see the online documentation at
  # https://docs.vagrantup.com.

  # Every Vagrant development environment requires a box. You can search for
  # boxes at https://atlas.hashicorp.com/search.
  config.vm.box = "centos/7"

  # Disable automatic box update checking. If you disable this, then
  # boxes will only be checked for updates when the user runs
  # `vagrant box outdated`. This is not recommended.
  # config.vm.box_check_update = false


  config.vm.network "private_network", type: "dhcp"

  config.vm.provision "shell", inline: <<-SHELL
    echo "gctbuild.vagrant" > /etc/hostname
    yum install -y -d1 git wget
    ln -s /vagrant /gct

    echo '#!/bin/sh' > /usr/local/bin/run_task
    echo 'exec sudo /gct/travis-ci/run_task_inside_docker.sh `id -u vagrant` "$@"' >> /usr/local/bin/run_task
    chmod +x /usr/local/bin/run_task

    echo 'Run "run_task $TASK [$COMPONENTS]" to do builds or tests.'
    echo 'For example, to build binary RPMs, run'
    echo '  run_task rpms'
    echo 'To run tests for the default build + ssh + gram, run'
    echo '  run_task tests ssh,gram'
    echo 'See .travis.yml and travis-ci/run_task_inside_docker.sh for more'
    echo 'details...'
  SHELL
end

