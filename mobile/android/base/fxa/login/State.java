/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.fxa.login;

import org.mozilla.gecko.fxa.login.FxAccountLoginStateMachine.ExecuteDelegate;
import org.mozilla.gecko.sync.ExtendedJSONObject;
import org.mozilla.gecko.sync.Utils;

public abstract class State {
  public static final long CURRENT_VERSION = 1L;

  public enum StateLabel {
    Promised,
    Engaged,
    Cohabiting,
    Married,
    Separated,
    Doghouse,
  }

  public enum Action {
    NeedsUpgrade,
    NeedsPassword,
    NeedsVerification,
    None,
  }

  protected final StateLabel stateLabel;
  public final String email;
  public final String uid;
  public final boolean verified;

  public State(StateLabel stateLabel, String email, String uid, boolean verified) {
    Utils.throwIfNull(email, uid);
    this.stateLabel = stateLabel;
    this.email = email;
    this.uid = uid;
    this.verified = verified;
  }

  public StateLabel getStateLabel() {
    return this.stateLabel;
  }

  public boolean isVerified() {
    return this.verified;
  }

  public ExtendedJSONObject toJSONObject() {
    ExtendedJSONObject o = new ExtendedJSONObject();
    o.put("version", State.CURRENT_VERSION);
    o.put("email", email);
    o.put("uid", uid);
    o.put("verified", verified);
    return o;
  }

  public State makeSeparatedState() {
    return new Separated(email, uid, verified);
  }

  public State makeDoghouseState() {
    return new Doghouse(email, uid, verified);
  }

  public abstract void execute(ExecuteDelegate delegate);

  public abstract Action getNeededAction();
}
