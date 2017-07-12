// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_STATE_H_
#define COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_STATE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"
#include "components/payments/content/payment_request.mojom.h"
#include "components/payments/content/payment_response_helper.h"

namespace i18n {
namespace addressinput {
class Storage;
class Source;
}  // namespace addressinput
}  // namespace i18n

namespace autofill {
class AutofillProfile;
class CreditCard;
class PersonalDataManager;
}  // namespace autofill

namespace payments {

class PaymentInstrument;
class PaymentRequestDelegate;
class PaymentRequestSpec;

// Keeps track of the information currently selected by the user and whether the
// user is ready to pay. Uses information from the PaymentRequestSpec, which is
// what the merchant has specified, as input into the "is ready to pay"
// computation.
class PaymentRequestState : public PaymentResponseHelper::Delegate {
 public:
  // Any class call add itself as Observer via AddObserver() and receive
  // notification about the state changing.
  class Observer {
   public:
    // Called when the information (payment method, address/contact info,
    // shipping option) changes.
    virtual void OnSelectedInformationChanged() = 0;

   protected:
    virtual ~Observer() {}
  };

  class Delegate {
   public:
    // Called when the PaymentResponse is available.
    virtual void OnPaymentResponseAvailable(
        mojom::PaymentResponsePtr response) = 0;

    // Called when the shipping option has changed to |shipping_option_id|.
    virtual void OnShippingOptionIdSelected(std::string shipping_option_id) = 0;

    // Called when the shipping address has changed to |address|.
    virtual void OnShippingAddressSelected(
        mojom::PaymentAddressPtr address) = 0;

   protected:
    virtual ~Delegate() {}
  };

  PaymentRequestState(PaymentRequestSpec* spec,
                      Delegate* delegate,
                      const std::string& app_locale,
                      autofill::PersonalDataManager* personal_data_manager,
                      PaymentRequestDelegate* payment_request_delegate);
  ~PaymentRequestState() override;

  // PaymentResponseHelper::Delegate
  void OnPaymentResponseReady(
      mojom::PaymentResponsePtr payment_response) override;

  // Returns whether the user has at least one instrument that satisfies the
  // specified supported payment methods.
  bool CanMakePayment() const;
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Initiates the generation of the PaymentResponse. Callers should check
  // |is_ready_to_pay|, which is inexpensive.
  void GeneratePaymentResponse();

  // Gets the Autofill Profile representing the shipping address or contact
  // information currently selected for this PaymentRequest flow. Can return
  // null.
  autofill::AutofillProfile* selected_shipping_profile() const {
    return selected_shipping_profile_;
  }
  autofill::AutofillProfile* selected_contact_profile() const {
    return selected_contact_profile_;
  }
  // Returns the currently selected instrument for this PaymentRequest flow.
  // It's not guaranteed to be complete. Returns nullptr if there is no selected
  // instrument.
  PaymentInstrument* selected_instrument() const {
    return selected_instrument_;
  }

  // Returns the appropriate Autofill Profiles for this user. The profiles
  // returned are owned by the PaymentRequestState.
  const std::vector<autofill::AutofillProfile*>& shipping_profiles() {
    return shipping_profiles_;
  }
  const std::vector<autofill::AutofillProfile*>& contact_profiles() {
    return contact_profiles_;
  }
  const std::vector<std::unique_ptr<PaymentInstrument>>&
  available_instruments() {
    return available_instruments_;
  }

  // Creates and adds an AutofillPaymentInstrument, which makes a copy of
  // |card|. |selected| indicates if the newly-created instrument should be
  // selected, after which observers will be notified.
  void AddAutofillPaymentInstrument(bool selected,
                                    const autofill::CreditCard& card);

  // Setters to change the selected information. Will have the side effect of
  // recomputing "is ready to pay" and notify observers.
  void SetSelectedShippingOption(const std::string& shipping_option_id);
  void SetSelectedShippingProfile(autofill::AutofillProfile* profile);
  void SetSelectedContactProfile(autofill::AutofillProfile* profile);
  void SetSelectedInstrument(PaymentInstrument* instrument);

  bool is_ready_to_pay() { return is_ready_to_pay_; }

  const std::string& GetApplicationLocale();
  autofill::PersonalDataManager* GetPersonalDataManager();
  std::unique_ptr<const ::i18n::addressinput::Source> GetAddressInputSource();
  std::unique_ptr<::i18n::addressinput::Storage> GetAddressInputStorage();

  Delegate* delegate() { return delegate_; }

 private:
  // Fetches the Autofill Profiles for this user from the PersonalDataManager,
  // and stores copies of them, owned by this PaymentRequestState, in
  // profile_cache_.
  void PopulateProfileCache();

  // Sets the initial selections for instruments and profiles, and notifies
  // observers.
  void SetDefaultProfileSelections();

  // Uses the user-selected information as well as the merchant spec to update
  // |is_ready_to_pay_| with the current state, by validating that all the
  // required information is available. Will notify observers.
  void UpdateIsReadyToPayAndNotifyObservers();

  // Notifies all observers that selected information has changed.
  void NotifyOnSelectedInformationChanged();

  // Returns whether the selected data satisfies the PaymentDetails requirements
  // (payment methods).
  bool ArePaymentDetailsSatisfied();
  // Returns whether the selected data satisfies the PaymentOptions requirements
  // (contact info, shipping address).
  bool ArePaymentOptionsSatisfied();

  bool is_ready_to_pay_;

  const std::string app_locale_;

  // Not owned. Never null. Both outlive this object.
  PaymentRequestSpec* spec_;
  Delegate* delegate_;
  autofill::PersonalDataManager* personal_data_manager_;

  autofill::AutofillProfile* selected_shipping_profile_;
  autofill::AutofillProfile* selected_contact_profile_;
  PaymentInstrument* selected_instrument_;

  // Profiles may change due to (e.g.) sync events, so profiles are cached after
  // loading and owned here. They are populated once only, and ordered by
  // frecency.
  std::vector<std::unique_ptr<autofill::AutofillProfile>> profile_cache_;
  std::vector<autofill::AutofillProfile*> shipping_profiles_;
  std::vector<autofill::AutofillProfile*> contact_profiles_;
  // Credit cards are directly owned by the instruments in this list.
  std::vector<std::unique_ptr<PaymentInstrument>> available_instruments_;

  PaymentRequestDelegate* payment_request_delegate_;

  std::unique_ptr<PaymentResponseHelper> response_helper_;

  base::ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(PaymentRequestState);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_STATE_H_
