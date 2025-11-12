import 'package:flutter/material.dart';

/// Helper class for consistent PostHog widget key naming conventions.
///
/// Using consistent key naming helps identify widgets in PostHog autocapture
/// and makes filtering easier in the PostHog dashboard.
///
/// Example:
/// ```dart
/// ElevatedButton(
///   key: PostHogKeys.button('add_item'),
///   onPressed: () => _addItem(),
///   child: Text('Add Item'),
/// )
/// ```
class PostHogKeys {
  /// Creates a key for a button widget.
  ///
  /// Example: `PostHogKeys.button('add_item')` → `ValueKey('btn_add_item')`
  static Key button(String name) => ValueKey('btn_$name');

  /// Creates a key for a menu widget.
  ///
  /// Example: `PostHogKeys.menu('file')` → `ValueKey('menu_file')`
  static Key menu(String name) => ValueKey('menu_$name');

  /// Creates a key for a card widget.
  ///
  /// Example: `PostHogKeys.card('product')` → `ValueKey('card_product')`
  static Key card(String name) => ValueKey('card_$name');

  /// Creates a key for an input/text field widget.
  ///
  /// Example: `PostHogKeys.input('search')` → `ValueKey('input_search')`
  static Key input(String name) => ValueKey('input_$name');

  /// Creates a key for a dialog widget.
  ///
  /// Example: `PostHogKeys.dialog('confirm')` → `ValueKey('dialog_confirm')`
  static Key dialog(String name) => ValueKey('dialog_$name');

  /// Creates a key for a tab widget.
  ///
  /// Example: `PostHogKeys.tab('home')` → `ValueKey('tab_home')`
  static Key tab(String name) => ValueKey('tab_$name');

  /// Creates a key for a list widget.
  ///
  /// Example: `PostHogKeys.list('products')` → `ValueKey('list_products')`
  static Key list(String name) => ValueKey('list_$name');

  /// Creates a key for a list item widget.
  ///
  /// Example: `PostHogKeys.listItem('product_123')` → `ValueKey('list_item_product_123')`
  static Key listItem(String name) => ValueKey('list_item_$name');

  /// Creates a key for an icon button widget.
  ///
  /// Example: `PostHogKeys.iconButton('search')` → `ValueKey('icon_btn_search')`
  static Key iconButton(String name) => ValueKey('icon_btn_$name');

  /// Creates a key for a checkbox widget.
  ///
  /// Example: `PostHogKeys.checkbox('agree_terms')` → `ValueKey('checkbox_agree_terms')`
  static Key checkbox(String name) => ValueKey('checkbox_$name');

  /// Creates a key for a radio button widget.
  ///
  /// Example: `PostHogKeys.radio('payment_method')` → `ValueKey('radio_payment_method')`
  static Key radio(String name) => ValueKey('radio_$name');

  /// Creates a key for a switch widget.
  ///
  /// Example: `PostHogKeys.switch_('notifications')` → `ValueKey('switch_notifications')`
  static Key switch_(String name) => ValueKey('switch_$name');

  /// Creates a key for a dropdown/select widget.
  ///
  /// Example: `PostHogKeys.dropdown('country')` → `ValueKey('dropdown_country')`
  static Key dropdown(String name) => ValueKey('dropdown_$name');

  /// Creates a key for a link widget.
  ///
  /// Example: `PostHogKeys.link('privacy_policy')` → `ValueKey('link_privacy_policy')`
  static Key link(String name) => ValueKey('link_$name');

  /// Creates a key for a custom widget type.
  ///
  /// Example: `PostHogKeys.custom('widget_type', 'name')` → `ValueKey('widget_type_name')`
  static Key custom(String type, String name) => ValueKey('${type}_$name');
}
