/*
 * Copyright (C) 2015-2016 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.cyanogenmod.settings.device;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.support.v14.preference.PreferenceFragment;
import android.support.v14.preference.SwitchPreference;
import android.support.v7.preference.Preference;
import android.provider.Settings;
import android.view.ViewGroup;

import cyanogenmod.providers.CMSettings;

public class DozeGestureFragment extends PreferenceFragment {

    private static final String KEY_AMBIENT_DISPLAY_ENABLE = "ambient_display_enable";
    private static final String KEY_GESTURE_HAND_WAVE = "gesture_hand_wave";
    private static final String KEY_GESTURE_PICK_UP = "gesture_pick_up";
    private static final String KEY_GESTURE_POCKET = "gesture_pocket";

    private Handler mGestureHandler = new Handler();

    private SwitchPreference mAmbientDisplayPreference;
    private SwitchPreference mHandwavePreference;
    private SwitchPreference mPickupPreference;
    private SwitchPreference mPocketPreference;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        addPreferencesFromResource(R.xml.touchscreen_panel);

        // Ambient Display
        boolean dozeEnabled = isDozeEnabled();
        mAmbientDisplayPreference = (SwitchPreference) findPreference(KEY_AMBIENT_DISPLAY_ENABLE);
        mAmbientDisplayPreference.setChecked(dozeEnabled);
        mAmbientDisplayPreference.setOnPreferenceChangeListener(mAmbientDisplayPrefListener);
        mHandwavePreference = (SwitchPreference) findPreference(KEY_GESTURE_HAND_WAVE);
        mHandwavePreference.setEnabled(dozeEnabled);
        mPickupPreference = (SwitchPreference) findPreference(KEY_GESTURE_PICK_UP);
        mPickupPreference.setEnabled(dozeEnabled);
        mPocketPreference = (SwitchPreference) findPreference(KEY_GESTURE_POCKET);
        mPocketPreference.setEnabled(dozeEnabled);
    }

    private boolean enableDoze(boolean enable) {
        return Settings.Secure.putInt(getContext().getContentResolver(),
                Settings.Secure.DOZE_ENABLED, enable ? 1 : 0);
    }

    private boolean isDozeEnabled() {
        return Settings.Secure.getInt(getContext().getContentResolver(),
                Settings.Secure.DOZE_ENABLED, 1) != 0;
    }

    private Preference.OnPreferenceChangeListener mAmbientDisplayPrefListener =
        new Preference.OnPreferenceChangeListener() {
        @Override
        public boolean onPreferenceChange(Preference preference, Object newValue) {
            boolean enable = (boolean) newValue;
            boolean ret = enableDoze(enable);
            if (ret) {
                mHandwavePreference.setEnabled(enable);
                mPickupPreference.setEnabled(enable);
                mPocketPreference.setEnabled(enable);
            }
            return ret;
        }
    };
}