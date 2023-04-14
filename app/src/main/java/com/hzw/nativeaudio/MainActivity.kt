package com.hzw.nativeaudio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.media.AudioManager
import android.os.Bundle
import android.view.View
import android.view.View.OnClickListener
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.SeekBar
import android.widget.SeekBar.OnSeekBarChangeListener
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.hzw.nativeaudio.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    companion object {
        init {
            System.loadLibrary("native-audio-jni")
        }

        private const val AUDIO_ECHO_REQUEST = 0
        private const val CLIP_NONE = 0
        private const val CLIP_HELLO = 1
        private const val CLIP_ANDROID = 2
        private const val CLIP_SAWTOOTH = 3
        private const val CLIP_PLAYBACK = 4
    }

    var uri: String? = null
    lateinit var assetManager: AssetManager

    var isPlayingAsset = false
    var isPlayingUri = false

    var numChannelsUri = 0

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        assetManager = assets

        createEngine()

        val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val sampleRate = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE).toInt()
        val bufSize =
            audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER).toInt()

        createBufferQueueAudioPlayer(sampleRate, bufSize)

        val uriAdapter = ArrayAdapter.createFromResource(
            this, R.array.uri_spinner_array, android.R.layout.simple_spinner_item
        )
        uriAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.apply {
            uriSpinner.apply {
                adapter = uriAdapter
                onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                    override fun onItemSelected(
                        parent: AdapterView<*>,
                        view: View,
                        pos: Int,
                        id: Long,
                    ) {
                        uri = parent.getItemAtPosition(pos).toString()
                    }

                    override fun onNothingSelected(parent: AdapterView<*>?) {
                        uri = null
                    }
                }
            }

            hello.setOnClickListener {
                selectClip(CLIP_HELLO, 5)
            }

            android.setOnClickListener {
                selectClip(CLIP_ANDROID, 7)
            }

            sawtooth.setOnClickListener {
                selectClip(CLIP_SAWTOOTH, 1)
            }

            var isReverbEnable = false
            reverb.setOnClickListener {
                isReverbEnable = !isReverbEnable
                if (!enableReverb(isReverbEnable)) {
                    isReverbEnable = !isReverbEnable
                }
            }

            embeddedSoundtrack.setOnClickListener(object : OnClickListener {
                var created = false
                override fun onClick(v: View?) {
                    if (!created) {
                        created = createAssetAudioPlayer(assetManager, "background.mp3")
                    }
                    if (created) {
                        isPlayingAsset = !isPlayingAsset
                        setPlayingAssetAudioPlayer(isPlayingAsset)
                    }
                }
            })

            uriSoundtrack.setOnClickListener(object : OnClickListener {
                var created = false
                override fun onClick(v: View?) {
                    if (!created && !uri.isNullOrEmpty()) {
                        created = createUriAudioPlayer(uri!!)
                    }
                }
            })

            pauseUri.setOnClickListener {
                setPlayingUriAudioPlayer(false)
            }
            playUri.setOnClickListener {
                setPlayingUriAudioPlayer(true)
            }
            loopUri.setOnClickListener(object : OnClickListener {
                var isLooping = false
                override fun onClick(v: View?) {
                    isLooping = !isLooping
                    setLoopingUriAudioPlayer(isLooping)
                }
            })
            muteLeftUri.setOnClickListener(object : OnClickListener {
                var muted = false
                override fun onClick(v: View?) {
                    muted = !muted
                    setChannelMuteUriAudioPlayer(0, muted)
                }
            })

            muteRightUri.setOnClickListener(object : OnClickListener {
                var muted = false
                override fun onClick(v: View?) {
                    muted = !muted
                    setChannelMuteUriAudioPlayer(1, muted)
                }
            })

            soloLeftUri.setOnClickListener(object : OnClickListener {
                var soloed = false
                override fun onClick(v: View?) {
                    soloed = !soloed
                    setChannelSoloUriAudioPlayer(0, soloed)
                }
            })
            soloRightUri.setOnClickListener(object : OnClickListener {
                var soloed = false
                override fun onClick(v: View?) {
                    soloed = !soloed
                    setChannelSoloUriAudioPlayer(1, soloed)
                }
            })

            muteUri.setOnClickListener(object : OnClickListener {
                var muted = false
                override fun onClick(v: View?) {
                    muted = !muted
                    setMuteUriAudioPlayer(muted)
                }
            })

            enableStereoPositionUri.setOnClickListener(object : OnClickListener {
                var enabled = false
                override fun onClick(v: View?) {
                    enabled = !enabled
                    enableStereoPositionUriAudioPlayer(enabled)
                }
            })

            channelsUri.setOnClickListener {
                if (numChannelsUri == 0) {
                    numChannelsUri = getNumChannelsUriAudioPlayer()
                }
                Toast.makeText(
                    this@MainActivity, "Channels: $numChannelsUri", Toast.LENGTH_SHORT
                ).show()
            }

            volumeUri.setOnSeekBarChangeListener(object : OnSeekBarChangeListener {
                var lastProgress = 100
                override fun onProgressChanged(
                    seekBar: SeekBar?,
                    progress: Int,
                    fromUser: Boolean,
                ) {
                    if (progress !in 0..100) {
                        throw AssertionError()
                    }
                    lastProgress = progress
                }

                override fun onStartTrackingTouch(seekBar: SeekBar?) {
                }

                override fun onStopTrackingTouch(seekBar: SeekBar?) {
                    val millibel = (100 - lastProgress) * -50
                    setVolumeUriAudioPlayer(millibel)
                }
            })

            panUri.setOnSeekBarChangeListener(object : OnSeekBarChangeListener {
                var lastProgress = 100
                override fun onProgressChanged(
                    seekBar: SeekBar?,
                    progress: Int,
                    fromUser: Boolean,
                ) {
                    if (progress !in 0..100) {
                        throw AssertionError()
                    }
                    lastProgress = progress
                }

                override fun onStartTrackingTouch(seekBar: SeekBar?) {
                }

                override fun onStopTrackingTouch(seekBar: SeekBar?) {
                    val millibel = (lastProgress - 50) * 20
                    setStereoPositionUriAudioPlayer(millibel)
                }
            })

            record.setOnClickListener {
                val status = ActivityCompat.checkSelfPermission(
                    this@MainActivity, Manifest.permission.RECORD_AUDIO
                )
                if (status != PackageManager.PERMISSION_GRANTED) {
                    ActivityCompat.requestPermissions(
                        this@MainActivity,
                        arrayOf(Manifest.permission.RECORD_AUDIO),
                        AUDIO_ECHO_REQUEST
                    )
                    return@setOnClickListener
                }
                recordAudio()
            }
            playback.setOnClickListener {
                selectClip(CLIP_PLAYBACK, 3)
            }
        }
    }

    private var isCreatedRecord = false
    private fun recordAudio() {
        if (!isCreatedRecord) {
            isCreatedRecord = createAudioRecorder()
        }
        if (isCreatedRecord) {
            startRecording()
        }
    }

    override fun onPause() {

        // turn off all audio
        selectClip(
            CLIP_NONE, 0
        )
        isPlayingAsset = false
        setPlayingAssetAudioPlayer(false)
        isPlayingUri = false
        setPlayingUriAudioPlayer(false)
        super.onPause()
    }

    override fun onDestroy() {
        shutdown()
        super.onDestroy()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        if (AUDIO_ECHO_REQUEST != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
            return
        }
        if (grantResults.size != 1 || grantResults[0] != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(
                applicationContext,
                getString(R.string.NeedRecordAudioPermission),
                Toast.LENGTH_SHORT
            ).show()
            return
        }
        recordAudio()
    }

    external fun createEngine()

    external fun createBufferQueueAudioPlayer(sampleRate: Int, samplesPerBuf: Int)

    external fun createAssetAudioPlayer(assetManager: AssetManager, fileName: String): Boolean

    external fun setPlayingAssetAudioPlayer(isPlaying: Boolean)

    external fun createUriAudioPlayer(uri: String): Boolean

    external fun setPlayingUriAudioPlayer(isPlaying: Boolean)

    external fun setLoopingUriAudioPlayer(isLooping: Boolean)

    external fun setChannelMuteUriAudioPlayer(chan: Int, mute: Boolean)

    external fun setChannelSoloUriAudioPlayer(chan: Int, solo: Boolean)

    external fun getNumChannelsUriAudioPlayer(): Int

    external fun setVolumeUriAudioPlayer(millibel: Int)

    external fun setMuteUriAudioPlayer(mute: Boolean)

    external fun enableStereoPositionUriAudioPlayer(enable: Boolean)

    external fun setStereoPositionUriAudioPlayer(permille: Int)

    external fun selectClip(which: Int, count: Int): Boolean

    external fun enableReverb(enable: Boolean): Boolean

    external fun createAudioRecorder(): Boolean

    external fun startRecording(): Boolean

    external fun shutdown()
}