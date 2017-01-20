/* Copyright (C) 2017 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "ti_phase.h"

#include "art_jvmti.h"
#include "base/macros.h"
#include "events-inl.h"
#include "runtime.h"
#include "runtime_callbacks.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "thread_list.h"

namespace openjdkjvmti {

jvmtiPhase PhaseUtil::current_phase_ = static_cast<jvmtiPhase>(0);

struct PhaseUtil::PhaseCallback : public art::RuntimePhaseCallback {
  inline static JNIEnv* GetJniEnv() {
    return reinterpret_cast<JNIEnv*>(art::Thread::Current()->GetJniEnv());
  }

  inline static jthread GetCurrentJThread() {
    art::ScopedObjectAccess soa(art::Thread::Current());
    return soa.AddLocalReference<jthread>(soa.Self()->GetPeer());
  }

  void NextRuntimePhase(RuntimePhase phase) OVERRIDE {
    // TODO: Events.
    switch (phase) {
      case RuntimePhase::kInitialAgents:
        PhaseUtil::current_phase_ = JVMTI_PHASE_PRIMORDIAL;
        break;
      case RuntimePhase::kStart:
        event_handler->DispatchEvent(nullptr, ArtJvmtiEvent::kVmStart, GetJniEnv());
        PhaseUtil::current_phase_ = JVMTI_PHASE_START;
        break;
      case RuntimePhase::kInit:
        {
          ScopedLocalRef<jthread> thread(GetJniEnv(), GetCurrentJThread());
          event_handler->DispatchEvent(nullptr,
                                       ArtJvmtiEvent::kVmInit,
                                       GetJniEnv(),
                                       thread.get());
          PhaseUtil::current_phase_ = JVMTI_PHASE_LIVE;
        }
        break;
      case RuntimePhase::kDeath:
        event_handler->DispatchEvent(nullptr, ArtJvmtiEvent::kVmDeath, GetJniEnv());
        PhaseUtil::current_phase_ = JVMTI_PHASE_DEAD;
        // TODO: Block events now.
        break;
    }
  }

  EventHandler* event_handler = nullptr;
};

PhaseUtil::PhaseCallback gPhaseCallback;

jvmtiError PhaseUtil::GetPhase(jvmtiEnv* env ATTRIBUTE_UNUSED, jvmtiPhase* phase_ptr) {
  if (phase_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }
  jvmtiPhase now = PhaseUtil::current_phase_;
  DCHECK(now == JVMTI_PHASE_ONLOAD ||
         now == JVMTI_PHASE_PRIMORDIAL ||
         now == JVMTI_PHASE_START ||
         now == JVMTI_PHASE_LIVE ||
         now == JVMTI_PHASE_DEAD);
  *phase_ptr = now;
  return ERR(NONE);
}

void PhaseUtil::SetToOnLoad() {
  DCHECK_EQ(0u, static_cast<size_t>(PhaseUtil::current_phase_));
  PhaseUtil::current_phase_ = JVMTI_PHASE_ONLOAD;
}

void PhaseUtil::SetToPrimordial() {
  DCHECK_EQ(static_cast<size_t>(JVMTI_PHASE_ONLOAD), static_cast<size_t>(PhaseUtil::current_phase_));
  PhaseUtil::current_phase_ = JVMTI_PHASE_ONLOAD;
}

void PhaseUtil::SetToLive() {
  DCHECK_EQ(static_cast<size_t>(0), static_cast<size_t>(PhaseUtil::current_phase_));
  PhaseUtil::current_phase_ = JVMTI_PHASE_LIVE;
}

void PhaseUtil::Register(EventHandler* handler) {
  gPhaseCallback.event_handler = handler;
  art::ScopedThreadStateChange stsc(art::Thread::Current(),
                                    art::ThreadState::kWaitingForDebuggerToAttach);
  art::ScopedSuspendAll ssa("Add phase callback");
  art::Runtime::Current()->GetRuntimeCallbacks()->AddRuntimePhaseCallback(&gPhaseCallback);
}


}  // namespace openjdkjvmti