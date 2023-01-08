pipeline {
    environment {
        SHORT_COMMIT = "${GIT_COMMIT[0..7]}"
    }
    
    agent {
        node {
            label 'android-boot-image-builder'
        }
    }

    stages {
        stage('Download kernel') {
            steps {
                    sh 'bash /opt/common/scripts/1_fetch_kernel.sh android_kernel_planet_mt6771 rooted-android'
                }
        }

        stage('Build kernel') {
            steps {
                    sh 'bash /opt/common/scripts/2_build_kernel.sh k71v1_64_bsp_defconfig'
                }
        }

        stage('Repackage kernel') {
            steps {
                    sh '''
                        bash /opt/common/scripts/3_copy_kernel.sh && \
                        bash /opt/common/scripts/4_fix_permissions.sh
                    '''
                }
        }

        stage('Root the Android Boot Image') {
            steps {
                sh 'bash /opt/magisk-tools/patch.sh /out/boot.img'
            }
        }

        stage('Copy boot image to master node') {
            steps {
                sh 'cp /out/root-boot.img "${WORKSPACE}/cosmo_root_boot_${SHORT_COMMIT}.img"'
            }
        }

        stage('Publish boot image on S3') {
            steps {
               archiveArtifacts artifacts: 'cosmo_root_boot_*.img', onlyIfSuccessful: true
            }
        }
  }
}
